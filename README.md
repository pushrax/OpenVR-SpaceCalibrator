# OpenVR Space Calibrator

Use tracked VR devices from one company with any other.

This is **experimental software** and may not work for your setup. It's well-tested for the Rift with Vive trackers though, with a community of a few hundred users ([Discord][]). It also seems to work for various HMDs (Windows MR, Rift) with Vive controllers and Knuckles prototypes. A quick video of how this works using an old version (~v0.3) is available at https://www.youtube.com/watch?v=W3TnQd9JMl4.

[Discord]: https://discord.gg/m7g2Wyj

### Install

Before following the directions below, download and run the installer for the [latest release](https://github.com/pushrax/OpenVR-SpaceCalibrator/releases). This will automatically set up SteamVR for use with multiple tracking systems (`activateMultipleDrivers: true`).

### Usage

Once Space Calibrator has a calibration, it works in the background to keep your devices configured correctly. Since v0.8, everything aside from creating the calibration is automated.

### Calibration

As part of first time setup, or when you make a change to your space (e.g. move a sensor), and occasionally as the calibration drifts over time (consumer VR tracking isn't perfectly stable), you'll need to run a calibration:

1. Copy the chaperone/guardian bounds from your HMD's play space. This doesn't need to be run if your HMD's play space hasn't changed since last time you copied it. __Example:__ if you're using the Rift with Vive trackers and you bump a Vive lighthouse, or if the calibration has just drifted a little, you likely don't need to run this step, but if you bump an Oculus sensor you will (after running Oculus guardian setup again).
    1. Run SteamVR, with only devices from your HMD's tracking system powered on. __Example:__ for Rift with Vive trackers, don't turn on the trackers yet.
    2. Open SPACE CAL in the SteamVR dashboard overlay.
    3. Click `Copy Chaperone Bounds to profile`. __Note:__ in the future when you need to update your chaperone/guardian setup, before doing so uncheck "Paste Chaperone Bounds automatically when geometry resets" in SPACE CAL, or it might overwrite your new chaperone with the old one! When you're finished editing, come back to SPACE CAL, copy the new bounds, then check the checkbox.

2. Calibrate devices.
    1. Open SteamVR if you haven't already. Turn on some or all your devices.
    2. Open SPACE CAL in the SteamVR dashboard overlay.
    3. Select one device from the reference space on the left and one device from the target space on the right. If you turned on multiple devices from one space and can't tell which one is selected, click "Identify selected devices" to blink an LED or vibrate it. __Example:__ for Rift with Vive trackers, you'll see the Touch controllers on the left, and Vive trackers on the right. __Pro tip:__ if you turn on just one Vive tracker, you don't have to figure out which one is selected.
    4. Hold these two devices in one hand, like they're glued together. If they slip, calibration won't work as well.
    5. Click `Start Calibration`
    6. Move and rotate your hand around slowly a few times, like you're calibrating the compass on your phone. You want to sample as many orientations as possible.
    7. Done! A profile will be saved automatically. If you haven't already, turn on all your devices. Space Calibrator will automatically apply the calibration to devices as they turn on.

### Calibration outside VR

You can calibrate without using the dashboard overlay by unminimizing Space Calibrator after opening SteamVR (it starts minimized). This is required if you're calibrating for a lone HMD without any devices in its tracking system.

### Compiling your own build

Open `OpenVR-SpaceCalibrator.sln` in Visual Studio 2015 and build. There are no external dependencies.

### The math

See [math.pdf](https://github.com/pushrax/OpenVR-SpaceCalibrator/blob/master/math.pdf) for details.
If you have some ideas for how to improve the calibration process, let me know!
