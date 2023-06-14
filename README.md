# NFS Most Wanted - manual LAN IP & Port window

This is an addon with 1 goal in mind - allow users to connect to a custom IP & port.

In addition to that, it also patches the game IP address reader and port binder to ignore the redirector from the server to allow connections over the internet.

## Usage

- Install ASI by copying it to the scripts directory

- Enter the LAN server browser

- While in LAN server browser, press 2 on the keyboard to invoke the window

- Input your IP/host (up to 32 characters) and port, then press "Connect", then input your name in the game window as per usual to continue the connection.

- If connecting to yourself, do not use "localhost" or "127.0.0.1" (despite that being the default). Use your adapter's local IP instead (192.168.x.x). This is due to server not broadcasting properly across all addresses.

- If you're doing ANY online interaction, tick the UPnP checkbox. In case UPnP is not available, open a UDP port `3658` manually. In case even that isn't an option, you will have to resort to a VPN service like Hamachi or Radmin.

- If any issues arise, restart the game. This is not a very polished area of the game to begin with.

## Standalone lobby server hosting

If you wish to host a standalone LAN lobby server without the game running, you can simply use my [NFSLAN server launcher](https://github.com/xan1242/NFSLAN).

## TODO

- fix a vanilla game bug - if you cancel a LAN connection, then exit out of the server browser, the game will crash

- fix another vanilla game bug - if you cancel a connection, you will not be able to reconnect anymore until it fails or succeeds again - I suspect that there's an improper socket shutdown

- fix localhost connection and maybe get the game to show the locally hosted server on the same machine

- or get the server advertisement somehow else working, maybe even make a server browser like Plutonium? (servers usually advertise on UDP port 9999)

- manual server/host ports (currently 3658)

- look at Underground 2

## Credits

- [ocornut/imgui: Dear ImGui: Bloat-free Graphical User interface for C++ with minimal dependencies](https://github.com/ocornut/imgui)
- [upredsun](https://www.codeproject.com/script/Membership/View.aspx?mid=3009516) - [Easy Port Forwarding and Managing Router with UPnP - CodeProject](https://www.codeproject.com/Articles/27237/Easy-Port-Forwarding-and-Managing-Router-with-UPnP) for upnpnat library (slightly modified by me)
- [berkayylmao (Berkay)](https://github.com/berkayylmao) - for WndProcWalker
