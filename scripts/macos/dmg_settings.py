# dmgbuild settings for the Osdrop DMG: the app and an Applications link to drag it onto.
# scripts/macos/release.sh passes the stapled app with -D app=<path to Osdrop.app>; dmgbuild
# provides `defines` when it runs this file.
import os.path

application = defines["app"]  # noqa: F821 - provided by dmgbuild
appname = os.path.basename(application)

format = "ULFO"
files = [application]
symlinks = {"Applications": "/Applications"}

volume_icon = os.path.join(application, "Contents", "Resources", "AppIcon.icns")
if os.path.exists(volume_icon):
    icon = volume_icon

background = "builtin-arrow"
window_rect = ((200, 120), (660, 400))
icon_size = 128
text_size = 14
icon_locations = {
    appname: (160, 180),
    "Applications": (480, 180),
}
