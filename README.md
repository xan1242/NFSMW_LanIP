# NFS Most Wanted - manual LAN IP & Port window

This is an addon with 1 goal in mind - allow users to connect to a custom IP & port.

In addition to that, it also patches the game IP address reader to ignore the redirector from the server to allow connections over the internet.

## Usage

- Install ASI by copying it to the scripts directory

- Enter the LAN server browser

- While in LAN server browser, press 2 on the keyboard to invoke the window

- Input your IP/host (up to 32 characters) and port, then press "Connect", then input your name in the game window as per usual to continue the connection.

- If connecting to yourself, do not use "localhost" or "127.0.0.1" (despite that being the default). Use your adapter's local IP instead (192.168.x.x). This is due to server not broadcasting properly across all addresses.

- If any issues arise, restart the game. This is not a very polished area of the game to begin with.

## TODO

- device reset crashes the game

- general Win32 jank and incompatibilities with XtendedInput (missing cursors, flickering window, etc.)

- fix a vanilla game bug - if you cancel a LAN connection, then exit out of the server browser, the game will crash

- fix another vanilla game bug - if you cancel a connection, you will not be able to reconnect anymore until it fails or succeeds again

- fix localhost connection and maybe get the game to show the locally hosted server on the same machine

- try to reduce the janky imgui patches to keep it inline with upstream (submodule it)

- look at Underground 2

## Credits

- [ocornut/imgui: Dear ImGui: Bloat-free Graphical User interface for C++ with minimal dependencies](https://github.com/ocornut/imgui)
