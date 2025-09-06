# OBS Clip Recorder

![App Icon](src/logo.png)

A minimalistic and efficient application for OBS Studio, designed to capture your best gaming moments with a simple hotkey, without compromising performance.

## 🎯 Our Goal

The primary goal of OBS Clip Recorder is to provide a **lightweight, fast, and stable solution for saving instant gameplay clips**. Unlike some built-in or third-party clipping tools, this application is optimized to minimize performance impact, ensuring a smooth experience for both streaming and recording.


## 🚀 Key Features

  * **Instant Clipping:** Save your last few minutes of gameplay with a single, customizable hotkey.
  * **Performance Optimized:** The application is built to be lean and efficient, with a minimal footprint on your CPU and GPU.
  * **Game-Specific Folders:** Clips are automatically organized into folders based on the game you're playing, making them easy to find.
  * **Simple & Intuitive UI:** A clean and straightforward user interface that integrates seamlessly with your OBS setup.


## 🛠️ How to Use and Setup

Follow these simple steps to get started with OBS Clip Recorder.

### 1\. Download and Installation (Pre-built)

If you're a user who just wants to get started, this is the easiest way.

1.  **Download the latest pre-built package** from the [Releases](https://www.google.com/search?q=https://github.com/Chirraaa/OBSReplayCompanion/releases) page.
2.  **Extract all files** from the downloaded `.zip` and **copy them directly into your main OBS Studio installation folder**. The default location is `C:\Program Files\obs-studio`.
3.  **Navigate to `C:\Program Files\obs-studio\obs-plugins\64bit`** and **create a new folder** named `saved`.
4.  **Move the following two files** into this new `saved` folder:
      * `frontend-tools.dll`
      * `frontend-tools.pdb`

### 2\. Building from Source

For developers or those who want to compile their own version of the application, follow these steps.

1.  **Follow the official OBS Studio build guide for Windows** to set up your environment and dependencies. You can find the guide here: [Build Instructions for Windows](https://github.com/obsproject/obs-studio/wiki/build-instructions-for-windows).

2.  After you've set up the build environment and cloned the main OBS repository, **copy this project's source files into a new folder named `obs-clip-recorder` inside the `external` folder** of the OBS Studio source tree. Your file structure should look like this:

3.  Follow the rest of the OBS build guide to compile the OBS Studio project. The clip recorder will be built as part of the process.

4.  Once the build is complete, you can find the necessary files in the `build/rundir/` directory. Proceed with the installation steps from the "Download and Installation" section above, using your newly compiled files.


## ⚙️ Behind the Scenes: Optimizations

This application is built with a focus on performance and reliability. Here are some of the key optimizations that make it so efficient:

  * **Asynchronous Clipping:** When you press the hotkey, the clipping process happens in the background. It doesn't block the main OBS thread, preventing any potential stutters or frame drops.
  * **Direct Buffer Access:** Instead of using complex video recording pipelines, the app directly accesses the OBS buffer to save clips, which is incredibly fast and efficient.
  * **Minimal Overhead:** The code is written in C++ and uses OBS's core APIs directly, ensuring that the application only uses the resources it absolutely needs.


## 🤝 Contributing

This is an open-source project, and contributions are welcome\! If you'd like to help improve OBS Clip Recorder, please feel free to open an issue or submit a pull request on GitHub.
