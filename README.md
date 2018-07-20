# OpenVR-SpaceCalibrator

Use VR devices from one company with any other.

This is **beta software** and may not work for you. A quick video walkthrough of the calibration process is available at https://www.youtube.com/watch?v=W3TnQd9JMl4

## Usage

### Calibration

If you don't already run a setup with multiple device types, see [Setting up SteamVR](#setting-up-steamvr).

1. Download and unzip the [latest release](https://github.com/pushrax/OpenVR-SpaceCalibrator/releases) of OpenVR-SpaceCalibrator.
2. Run SteamVR, and turn on your Touch controllers and only one Vive device to use for calibration.
3. Run OpenVR-SpaceCalibrator.
4. Hold the left Touch controller and your Vive device in your left hand securely, like they're glued together.
5. Click "Start Calibration"
6. Wave your left hand around, like you're calibrating the compass on your phone. You want to get as many orientations as possible.
7. Done! A profile will be saved automatically. You can turn on the rest of your Vive devices now.

Next time you run SteamVR, open OpenVR-SpaceCalibrator and it will load the calibration automatically and apply it devices you turn on.


### Setting up SteamVR

Every hardware designer with their own tracking system needs to make a "driver"
so SteamVR can see their tracking data. SteamVR by default will load only one driver at a time,
but we can tell it to load all of them by editing the vrsettings config file.

Open `Steam\steamapps\common\SteamVR\resources\settings\default.vrsettings` in a text editor,
look for the line that has `"activateMultipleDrivers": false` and change the `false` to `true`.
In some guides online you may also see references to `requireHmd`, this does _not_ need to be changed
unless you truly want to be able to run without any kind of HMD.

Sometimes when SteamVR updates this file seems to get wiped, if your setup stops working you might
have to edit the config again.


### Compiling your own build

1. Open `OpenVR-SpaceCalibrator.sln` in Visual Studio 2015 and build.


## The math

See [math.pdf](https://github.com/pushrax/OpenVR-SpaceCalibrator/blob/master/math.pdf) for details.
If you have some ideas for how to improve the calibration process, let me know!
