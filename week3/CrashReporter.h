/** @file CrashReporter.h
 *
 *  EEEE2076 - Software Engineering & VR Project (Group 13)
 *
 *  Captures Qt messages and unhandled crashes (std::terminate / Win32
 *  access violations) and shows a dialog with the recent log so a demo
 *  failure produces a visible report instead of a silent process death.
 */
#ifndef CRASH_REPORTER_H
#define CRASH_REPORTER_H

#include <QString>

class CrashReporter {
public:
  /** Install Qt message handler, std::terminate handler, and (on
   *  Windows) the unhandled SEH filter. Also opens the log file next
   *  to the exe. Call once, early in main(), before the QApplication
   *  starts processing events. */
  static void install();

  /** Path of the log file we are appending to. Useful for the About
   *  dialog or status messages. */
  static QString logFilePath();

  /** Show the crash dialog with `details` plus the last N captured
   *  log lines. Safe to call from any thread that has a live event
   *  loop; otherwise it falls back to a native message box. */
  static void showCrashDialog(const QString &title, const QString &details);
};

#endif
