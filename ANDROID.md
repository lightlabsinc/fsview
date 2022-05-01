# Android integration

## Basics

Android is the historical target environment of `FSView`, and viewing/downloading large media from a connected Android device in an efficient way was its motivational use case. Given the diversity (including temporal diversity, i.e. swift evolution) of Android devices and solutions, we deliberatley decided to provide only the most generic integration hints (as opposed to rolling out a complete add-on covering a number of system and otherwise privileged packages). We assume that the target hardware is a consumer electronic device and the user has access to its GUI (particularly, the USB mode selection option). The specific instructions refer to Android Marshmallow (6.x); you may need to locate moved or refactored system components within your target firmware source tree yourself.

In addition to Android programming basics, you need to be familiar with [the `init.rc` syntax and the Android boot process](https://android.googlesource.com/platform/system/core/+/master/init/README.md).

Even before any firmware change you can play with `fsview_*` tools in unprivileged userspace (they will be limited to operation on regular files).

## Jump right in!

### Kernel preparation

* Set the desired mass storage gadget parameters in the kernel command line (e.g. `g_android.luns=3 g_android.removable=1,1,1 g_android.ro=1,1,0`).
* Use the [advised single-line kernel modification](README.md) if you'd like to expose large (>2Gb) file collections via CDFS.

### Init sequence preparation

See [init](init) for example `*.rc` and shell scripts to place to the root directory. `init.fsv.sync.rc` governs the state machine that erects the ephemeral filesystem on a mapped block device and sets that device as the storage file exposed by the gadget driver; this state machine will be explained below in detail.

In `init.rc`, in `on property:vold.decrypt=trigger_load_persist_props`, add the following line at the very beginning:

    exec - -- /system/bin/sh /init.fsv.sync.sh

It would mirror the decrypted data partition and move the filesystem mount onto the mirror ("offshoot").

### Reenabling the mass storage USB mode in the GUI

To unlock mass storage in developer settings, check out the `platform/frameworks/base` project and navigate to `packages/SettingsLib/res/values/arrays.xml`. (`SettingsLib` was a part of `Settings` in older releases.) Add the following option to `usb_configuration_values`:

        <!-- Do not translate. -->
        <item>mass_storage</item>

and the following option at the *exactly matching list location* in `usb_configuration_titles`:

        <item>Browse media collection as a virtual storage device</item>

To unlock mass storage in user settings, stay in `SettingsLib` and add the following strings in `strings.xml`:

    <!-- Title... -->
    <string name="usb_use_fsview_transfers">Virtual drive</string>
    <!-- Description... -->
    <string name="usb_use_fsview_transfers_desc">Browse media collection as a virtual storage device</string>
    
Re-add the following modes to `DEFAULT_MODES` in `UsbChooserActivity.java`:

    UsbBackend.MODE_POWER_SINK | UsbBackend.MODE_DATA_MSG,

and modify `getTitle` and `getSummary` to return the newly added summary and description.

In `deviceinfo/UsbBackend.java` reenable MSG in `getUsbDataMode`:

        } else if (mUsbManager.isFunctionEnabled(UsbManager.USB_FUNCTION_MASS_STORAGE)) {
            return MODE_DATA_MSG;

Reenable it in `setUsbFunction`:

            case MODE_DATA_MSG:
                mUsbManager.setCurrentFunction(UsbManager.USB_FUNCTION_MASS_STORAGE);
                mUsbManager.setUsbDataUnlocked(true);
                break;

Make sure that `isModeSupported` returns `true` for `MODE_DATA_MSG`.

Note: you can switch to mass storage mode simply by setting `sys.usb.config` to `mass_storage,adb` on the command line.

### Wiring a system property to USB connection status and mode

Choose a name for the system property to control the ephemeral FS status. It would be a good idea to make it vendor-specific, e.g.

    public static final String PROPERTY_SYNC_ENABLE = "light.sync.enable";

The example [android/MSGModeObserver](connection observer) application registers a USB mode receiver upon boot (since that can only be done in code rather than with metadata) and sets the `light.sync.enable` property to 1 or 0 based on the USB connection state (the linger timeout is used to stabilize the response to a flurry of USB notification broadcasts).

### Configuring the media library

The `light.sync.folders` property governs which folders contain the media library. The property is being used in `init.fsv.sync_service.sh`. Currently it's a single folder.

### Setting up disconnection hooks

Among the provided example scripts, `init.fsv.sync_handoff.sh` starts `HandOffService` to perform any desired post-disconnection actions based on the user-modified contents of the scratch disk image. If you don't need any post-disconnection cleanup, you can remove the call to `/system/bin/am startservice` or, even better, the entire runlevel of the state machine.

### Putting the tools in

Build all the six `fsview_*` binaries and place them in `/sbin`.

## So how does it work when I plug it in?

The state machine in `init.fsv.sync.rc` is driven by two properties: `light.sync.status` (current micro-state) and `light.sync.enable` (desired macro-state). The latter determines whether the connection goes up or down.

### Going up

#### 0 -> 1

"Magic happens here". `fsview_mkfs` examines the extents of the files making up the media collection, creates HFS+ metadata in ZRAM (compressed RAM) and combines in-memory and on-disk data into a functional readonly hybrid (HFS+ and CDFS) filesystem accessible via a mapper block device. This device will be exposed via `lun0` as non-CD storage readable by Mac OS.

#### 1 -> 2

`fsview_fork` creates a mirror drive from the one created above, zeroing out the first 16 sectors, and therefore exposing only CDFS metadata. This device will be exposed via `lun1` as a CD storage readable by Windows and Linux (the need to erase the leading sectors, and therefore the need for two separate mass storage devices, comes from inability of certain Linux distros/configurations to decide which file system to mount the hybrid disk as.)

#### 2 -> 3

Once the media collection is ready for presentation, a scratch FAT32 disk image is created by `fsview_temp` to store uploads, commands or metadata (e.g. which files in the media collection are no longer needed) passed from the host machine. You can use `mkfs.vfat` as an alternative. If no information needs to be uploaded, you can skip this step.

#### 3 -> 4

The newly created storage devices are passed to the mass storage gadget driver.

### Going down

#### 4 -> 3

The mass storage gadget driver shuts down all the exposed LUNs (`lun`, `lun0` and `lun1`, in reverse order).

#### 3 -> 2

The scratch image (now free from host system interference) is passed to the handoff service. The specifics of how to parse a FAT32 image file in userspace are beyond this tutorial (there are open source libraries allowing exactly that). 

#### 2 -> 1

The CDFS image (known to device-mapper as `virtualcd`) is torn down. 

#### 1 -> 0

The HFS+ image (known to device-mapper as `virtualhd`) is torn down, and the file descriptors retained when it was created are freed. If any exposed file in the media collection has been deleted (whether or not a new file has been created under its name in its folder), now is the moment when its inode (rather than its dirent) is actually deleted and its disk footprint is reclaimed.
