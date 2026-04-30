# Group 13 – VR‑Enabled CAD Viewer  
### Team Members
- **Bekr Alshukry**
- **Oluwatoniloba Adeyinka**
- **Xuankai Zhang**
- **Zijian Wang**

## Overview
This project is the Group Design Project for **EEEE2076 – Software Development**.  
It extends the individual Qt/VTK worksheets by adding:

- A full **GUI application**
- **VR rendering** using OpenVR
- **Filters**, **lighting**, **colour editing**, and **tree‑based model management**
- A complete **Windows installer** (NSIS)
- **Doxygen documentation** and **GitHub Pages** hosting

---

## Features

### Basic Application Features
- Qt‑based GUI  
- TreeView for model organisation  
- Status bar feedback  
- Reset view, colour picker, visibility toggles  

### CAD Loading
- Load individual STL files  
- Load multiple models at once  
- Optional: load all STL files from a directory  

### CAD Editing
- Rename model parts  
- Change colour  
- Toggle visibility  
- Apply filters:
  - Clip filter  
  - Shrink filter  

### 🔹 VR Functionality
- Start/stop VR session  
- Independent VR rendering thread  
- Model interaction using VR controllers  
- Real‑time updates from GUI to VR (colour, filters, visibility)
