/**
 * @file CrashReporter.cpp
 * @brief Implements crash logging, Qt message capture, and crash dialogs.
 */

#include "CrashReporter.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMutex>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QVBoxLayout>

#include <exception>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dbghelp.h>
#  pragma comment(lib, "Dbghelp.lib")
#endif

namespace {

constexpr int kRingBufferLines = 400;

QMutex g_mutex;
QStringList g_ring;
QString g_logFilePath;
QtMessageHandler g_previousHandler = nullptr;

/**
 * @brief Converts a Qt message type to a fixed-width log label.
 * @param type Qt message severity.
 * @return Text label for the severity.
 */
QString severityTag(QtMsgType type) {
  switch (type) {
  case QtDebugMsg:    return QStringLiteral("DEBUG");
  case QtInfoMsg:     return QStringLiteral("INFO ");
  case QtWarningMsg:  return QStringLiteral("WARN ");
  case QtCriticalMsg: return QStringLiteral("CRIT ");
  case QtFatalMsg:    return QStringLiteral("FATAL");
  }
  return QStringLiteral("?    ");
}

/**
 * @brief Adds a line to the in-memory ring buffer and log file.
 * @param line Log line to append.
 */
void appendToRingAndFile(const QString &line) {
  QMutexLocker lock(&g_mutex);
  g_ring.append(line);
  while (g_ring.size() > kRingBufferLines)
    g_ring.removeFirst();

  if (!g_logFilePath.isEmpty()) {
    QFile f(g_logFilePath);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
      QTextStream ts(&f);
      ts << line << '\n';
    }
  }
}

QString joinedRing() {
  QMutexLocker lock(&g_mutex);
  return g_ring.join(QChar('\n'));
}

/**
 * @brief Qt message hook that captures qDebug/qWarning/qCritical output.
 * @param type Message severity.
 * @param ctx Source context supplied by Qt.
 * @param msg Message text.
 */
void qtMessageHandler(QtMsgType type, const QMessageLogContext &ctx,
                      const QString &msg) {
  const QString line =
      QStringLiteral("%1 %2 [%3] %4")
          .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
          .arg(severityTag(type))
          .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
          .arg(msg);
  appendToRingAndFile(line);
  if (ctx.file && ctx.file[0] != '\0') {
    appendToRingAndFile(QStringLiteral("    at %1:%2 (%3)")
                            .arg(QString::fromUtf8(ctx.file))
                            .arg(ctx.line)
                            .arg(QString::fromUtf8(ctx.function ? ctx.function
                                                                : "")));
  }

  if (g_previousHandler)
    g_previousHandler(type, ctx, msg);

  if (type == QtFatalMsg) {
    CrashReporter::showCrashDialog(QObject::tr("Fatal error"),
                                   QObject::tr("qFatal: %1").arg(msg));
    /* qFatal already aborts after the handler returns on most builds, but
     * if it doesn't we want to surface the dialog before the process dies. */
  } else if (type == QtCriticalMsg) {
    /* Don't pop the dialog for every critical - critical messages are
     * common from VTK on non-fatal issues and would spam during a demo.
     * The log file still has them. */
  }
}

/**
 * @brief std::terminate handler used for uncaught C++ exceptions.
 */
void terminateHandler() {
  QString details = QObject::tr("Unhandled C++ exception (std::terminate).");
  try {
    auto eptr = std::current_exception();
    if (eptr) {
      try {
        std::rethrow_exception(eptr);
      } catch (const std::exception &e) {
        details += QStringLiteral("\nwhat(): %1").arg(QString::fromUtf8(e.what()));
      } catch (...) {
        details += QStringLiteral("\n(non-std exception)");
      }
    }
  } catch (...) {
    /* current_exception itself failed; nothing more to say. */
  }

  appendToRingAndFile(QStringLiteral("=== std::terminate === ") + details);
  CrashReporter::showCrashDialog(QObject::tr("Application terminated"), details);
  std::abort();
}

#ifdef Q_OS_WIN

/**
 * @brief Formats a Windows structured-exception code for display.
 * @param code Native exception code.
 * @return Human-readable exception name.
 */
QString formatExceptionCode(DWORD code) {
  switch (code) {
  case EXCEPTION_ACCESS_VIOLATION:      return QStringLiteral("ACCESS_VIOLATION");
  case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return QStringLiteral("ARRAY_BOUNDS_EXCEEDED");
  case EXCEPTION_BREAKPOINT:            return QStringLiteral("BREAKPOINT");
  case EXCEPTION_DATATYPE_MISALIGNMENT: return QStringLiteral("DATATYPE_MISALIGNMENT");
  case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return QStringLiteral("FLT_DIVIDE_BY_ZERO");
  case EXCEPTION_ILLEGAL_INSTRUCTION:   return QStringLiteral("ILLEGAL_INSTRUCTION");
  case EXCEPTION_INT_DIVIDE_BY_ZERO:    return QStringLiteral("INT_DIVIDE_BY_ZERO");
  case EXCEPTION_PRIV_INSTRUCTION:      return QStringLiteral("PRIV_INSTRUCTION");
  case EXCEPTION_STACK_OVERFLOW:        return QStringLiteral("STACK_OVERFLOW");
  default:                              return QStringLiteral("0x%1").arg(code, 8, 16, QChar('0'));
  }
}

/**
 * @brief Captures a best-effort native stack trace on Windows.
 * @param ctx CPU context provided by the SEH exception record.
 * @return Text stack trace.
 */
QString captureStackWalk(CONTEXT *ctx) {
  HANDLE proc = GetCurrentProcess();
  HANDLE thread = GetCurrentThread();
  static bool symInit = false;
  if (!symInit) {
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES |
                  SYMOPT_UNDNAME);
    SymInitialize(proc, nullptr, TRUE);
    symInit = true;
  }

  STACKFRAME64 frame{};
  DWORD machine;
#ifdef _M_X64
  machine = IMAGE_FILE_MACHINE_AMD64;
  frame.AddrPC.Offset    = ctx->Rip;
  frame.AddrFrame.Offset = ctx->Rbp;
  frame.AddrStack.Offset = ctx->Rsp;
#else
  machine = IMAGE_FILE_MACHINE_I386;
  frame.AddrPC.Offset    = ctx->Eip;
  frame.AddrFrame.Offset = ctx->Ebp;
  frame.AddrStack.Offset = ctx->Esp;
#endif
  frame.AddrPC.Mode    = AddrModeFlat;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Mode = AddrModeFlat;

  QString out;
  for (int i = 0; i < 64; ++i) {
    if (!StackWalk64(machine, proc, thread, &frame, ctx, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
      break;
    if (frame.AddrPC.Offset == 0)
      break;

    DWORD64 displacement = 0;
    char nameBuf[sizeof(SYMBOL_INFO) + 512] = {};
    auto sym = reinterpret_cast<SYMBOL_INFO *>(nameBuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 511;

    QString fnName = QStringLiteral("?");
    if (SymFromAddr(proc, frame.AddrPC.Offset, &displacement, sym))
      fnName = QString::fromUtf8(sym->Name);

    IMAGEHLP_LINE64 lineInfo{};
    lineInfo.SizeOfStruct = sizeof(lineInfo);
    DWORD lineDisp = 0;
    QString file;
    int lineNo = 0;
    if (SymGetLineFromAddr64(proc, frame.AddrPC.Offset, &lineDisp, &lineInfo)) {
      file = QString::fromUtf8(lineInfo.FileName);
      lineNo = lineInfo.LineNumber;
    }

    out += QStringLiteral("  [%1] 0x%2 %3")
               .arg(i, 2)
               .arg(quint64(frame.AddrPC.Offset), 16, 16, QChar('0'))
               .arg(fnName);
    if (!file.isEmpty())
      out += QStringLiteral("   %1:%2").arg(file).arg(lineNo);
    out += QChar('\n');
  }
  return out;
}

/**
 * @brief Windows SEH crash filter that logs native crashes before exit.
 * @param ep Native exception details.
 * @return Exception handling decision for Windows.
 */
LONG WINAPI sehFilter(EXCEPTION_POINTERS *ep) {
  const DWORD code = ep->ExceptionRecord->ExceptionCode;
  const void *addr = ep->ExceptionRecord->ExceptionAddress;

  QString summary =
      QStringLiteral("Native crash: %1 at 0x%2")
          .arg(formatExceptionCode(code))
          .arg(reinterpret_cast<quintptr>(addr), 0, 16);

  if (code == EXCEPTION_ACCESS_VIOLATION &&
      ep->ExceptionRecord->NumberParameters >= 2) {
    const ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0];
    const ULONG_PTR badAddr = ep->ExceptionRecord->ExceptionInformation[1];
    summary += QStringLiteral(" (%1 at 0x%2)")
                   .arg(op == 0 ? QStringLiteral("read")
                                : op == 1 ? QStringLiteral("write")
                                          : QStringLiteral("execute"))
                   .arg(quintptr(badAddr), 0, 16);
  }

  appendToRingAndFile(QStringLiteral("=== SEH === ") + summary);

  QString stack = captureStackWalk(ep->ContextRecord);
  if (!stack.isEmpty())
    appendToRingAndFile(QStringLiteral("Stack:\n") + stack);

  /* Calling into Qt from here is risky (heap state may be corrupt and we
   * are likely on a non-GUI thread). Use the native MessageBox so the
   * user sees something even if Qt is unsafe to touch. */
  QString body = summary + QStringLiteral("\n\nLog file:\n") +
                 g_logFilePath +
                 QStringLiteral("\n\nRecent log:\n") + joinedRing();
  if (!stack.isEmpty())
    body += QStringLiteral("\n\nStack:\n") + stack;

  MessageBoxW(nullptr, reinterpret_cast<const wchar_t *>(body.utf16()),
              L"Group 13 - VR Viewer crashed",
              MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);

  return EXCEPTION_EXECUTE_HANDLER;
}

#endif // Q_OS_WIN

/**
 * @class CrashDialog
 * @brief Modal Qt dialog that presents crash details and recent log lines.
 */
class CrashDialog : public QDialog {
public:
  /**
   * @brief Builds the crash dialog UI.
   * @param title Dialog title and headline.
   * @param details Crash summary.
   * @param log Recent captured log lines.
   * @param parent Optional parent widget.
   */
  CrashDialog(const QString &title, const QString &details, const QString &log,
              QWidget *parent = nullptr)
      : QDialog(parent) {
    setWindowTitle(title);
    resize(800, 500);

    auto *layout = new QVBoxLayout(this);
    auto *header = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()), this);
    layout->addWidget(header);

    auto *summary = new QPlainTextEdit(this);
    summary->setReadOnly(true);
    summary->setPlainText(details);
    summary->setMaximumHeight(120);
    layout->addWidget(summary);

    auto *logLabel = new QLabel(QObject::tr("Recent log (saved to %1):")
                                    .arg(g_logFilePath),
                                this);
    logLabel->setWordWrap(true);
    layout->addWidget(logLabel);

    auto *logView = new QPlainTextEdit(this);
    logView->setReadOnly(true);
    logView->setPlainText(log);
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(logView, 1);

    auto *buttons = new QDialogButtonBox(this);
    auto *copyBtn = buttons->addButton(QObject::tr("Copy to Clipboard"),
                                       QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    QObject::connect(copyBtn, &QPushButton::clicked, this,
                     [this, details, log]() {
                       QString full = details + QStringLiteral("\n\n") + log;
                       QApplication::clipboard()->setText(full);
                     });
    QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    QObject::connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::close);
    layout->addWidget(buttons);
  }
};

} // namespace

/**
 * @brief Installs message handlers and prepares the crash log file.
 */
void CrashReporter::install() {
  /* Pick a log file we can actually write to: %LOCALAPPDATA%/Group13_VRViewer
   * (with /tmp-style fallback) so a USB-stick / read-only install still
   * produces a usable log. */
  QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (dir.isEmpty())
    dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  QDir().mkpath(dir);
  g_logFilePath = dir + QStringLiteral("/crash.log");

  /* Truncate so each run starts clean - otherwise the log grows forever. */
  QFile f(g_logFilePath);
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
    QTextStream ts(&f);
    ts << "Group 13 VR Viewer log started "
       << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << '\n';
  }

  g_previousHandler = qInstallMessageHandler(qtMessageHandler);
  std::set_terminate(terminateHandler);

#ifdef Q_OS_WIN
  SetUnhandledExceptionFilter(sehFilter);
#endif
}

/**
 * @brief Returns the path currently used for crash logging.
 * @return Crash log path.
 */
QString CrashReporter::logFilePath() { return g_logFilePath; }

/**
 * @brief Displays or logs crash details depending on thread safety.
 * @param title Dialog title.
 * @param details Crash details.
 */
void CrashReporter::showCrashDialog(const QString &title, const QString &details) {
  const QString log = joinedRing();

  /* If we are not on the GUI thread, or there is no QApplication yet,
   * fall back to a native message box - QDialog needs the GUI thread. */
  QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance());
  const bool guiSafe =
      app && QThread::currentThread() == app->thread();

  if (guiSafe) {
    CrashDialog dlg(title, details, log);
    dlg.exec();
    return;
  }

#ifdef Q_OS_WIN
  QString body = details + QStringLiteral("\n\nLog:\n") + log;
  MessageBoxW(nullptr, reinterpret_cast<const wchar_t *>(body.utf16()),
              reinterpret_cast<const wchar_t *>(title.utf16()),
              MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
#else
  /* Best-effort: append to log only. */
  appendToRingAndFile(QStringLiteral("[crash dialog skipped - non-GUI thread] ") +
                      title + QStringLiteral(": ") + details);
#endif
}
