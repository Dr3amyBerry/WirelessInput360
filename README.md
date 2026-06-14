# A lightweight Xbox 360 plugin that recieves controller input from PC over the network.

## BadAvatar / XeUnshackle status

Confirmed working by Dreamy from Rigor Core on a BadAvatar / XeUnshackle setup over Ethernet.

See [`docs/badavatar-xeunshackle-notes.md`](docs/badavatar-xeunshackle-notes.md) for the technical notes, root cause, working USB layout, and configuration details.

## 📜 Features
- Send controller input to an Xbox 360 from PC over TCP.
- Support for multiple controllers.

## ⚙️ Usage

1. Download both the Windows App & the Xbox Plugin from the latest [Release](https://github.com/UncreativeXenon/WirelessInput360/releases).
2. For the Windows app, edit `port.txt` to the port where the server will listen to (default is 3000, you can leave it at that if it works).
3. For the Xbox Plugin, edit the IP `WirelessInput360.ini` to your PC's Local IPv4 and the port to whatever port you set on Windows (default is 3000).
4. make sure `WirelessInput360.ini` is placed right next to `WirelessInput360.xex` and add the `.xex` to Plugin list in DashLaunch configuration.
5. Run the Windows App, make sure the connection with the Xbox succeeds and connect your controller(s) to the PC.

## 🧱 Credits

Based on and wouldn't have been possible without EinTim23's [hiddriver360](https://github.com/EinTim23/hiddriver360) repo.

## 📄 License

MIT License.  
See [LICENSE](LICENSE) for full details.
