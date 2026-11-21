# Troubleshooting

> Status: outline. Entries are added as the features they cover land.

## Windows: SmartScreen warns about the installer

Osdrop's Windows builds are not code-signed yet, so SmartScreen shows "Windows protected your PC".
Choose **More info → Run anyway** only if you downloaded the installer from the official GitHub
Releases page. Signing through SignPath Foundation is planned.

## Windows: an antivirus product flags Osdrop

Osdrop reads the keyboard and mouse and types and clicks on behalf of another computer, which is
also what malicious keyloggers do. Unsigned builds are more likely to be flagged. Report false
positives to the antivirus vendor and, if you can, open an issue with the product name and version.

## Windows: a drag from an app run as administrator does not cross

Windows keeps apps that run as administrator from dropping onto apps that do not (User Interface
Privilege Isolation), and Osdrop's drop targets at the screen edges run as you. A drag that starts
in an elevated app, such as an administrator PowerShell or an installer, cannot be carried to
another computer; the edge acts as a wall for it. Drag the files from Explorer instead, or run the
app without administrator rights.

## Mac: the cursor stutters over Wi-Fi

macOS takes the Wi-Fi radio off your network's channel at intervals for AWDL, the link behind
AirDrop, AirPlay and Sidecar, and the other computer's packets wait meanwhile. Turn on
**Settings › Advanced › Improve the connection over Wi-Fi**. The first time, macOS asks you to
allow Osdrop in **System Settings › General › Login Items › Allow in the Background**; a small
helper inside Osdrop then keeps AWDL off while Osdrop runs. While it is on, AirDrop, AirPlay and
Sidecar do not work. Turning the setting off or quitting Osdrop brings AWDL back within seconds.
A wired connection on either computer does not avoid this: the Mac's own radio is the one that
leaves the channel.

## Osdrop and Osplay together

Osplay's virtual displays are ordinary monitors to Osdrop: they appear in the arrangement in
Settings › Arrangement and the cursor crosses through them like through any other monitor. When
Osplay ends them, Osdrop drops them from the arrangement within a second. Osplay shows a Mac's
virtual display on a PC as a picture of it, so a click on that picture does nothing: move to the
PC's own monitors to control the PC, or to the virtual display on the Mac to work there.

