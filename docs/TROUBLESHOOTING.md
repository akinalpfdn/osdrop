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
