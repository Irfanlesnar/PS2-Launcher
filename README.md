# PS2 Launcher

A modern PlayStation 2 game launcher focused on delivering a cleaner, faster, and more console-like experience - Designed for real PS2 hardware

<img width="1920" height="1080" alt="4 0" src="https://github.com/user-attachments/assets/42deae63-18f8-4372-addf-3e29a01bfe59" />

### About
- **PS2 Launcher**
    - Games tab shows list of all games from all connected source in one place
    - Apps tab helps to
        - Launch ELF files
        - Launch PS1 games
    - Settings tab helps to
        - Download covers
        - SMB setup
        - Launcher modifications

- **Supported Devices**
    - USB FAT32
    - USB exFAT
    - USB HDD FAT32
    - USB HDD exFAT
    - MX4SIO
    - Internal HDD APA FAT32
    - Internal HDD exFAT GPT/MBR
    - SMB

- **Virtual Multitap support for up to 4 players without a physical multitap**
    - Player 1 → PS2 Port 1
    - Player 2 → PS2 Port 2
    - Player 3 → USB Port 1
    - Player 4 → USB Port 2
  
  For best compatibility, connect all required controllers before launching the game.

- **Advanced controller support (wired only)**
    - PS5 DualSense controller
    - PS4 DualShock 4 controller
    - PS3 DualShock 3 controller
    - Xbox series s controller
    - Xbox one controller
 
  Controller behavior
    - PS2 port 1 connected with PS2 controller
        - PS2 port 1 controller plays as player 1
    - PS2 port 1 and PS2 port 2 connected with PS2 controller
        -  PS2 port 1 controller plays as player 1
        -  PS2 port 2 controller plays as player 2
    - 2 PS2 controllers and 1 USB controller
        - PS2 port 1 controller plays as player 1
        - PS2 port 2 controller plays as player 2
        - USB controller plays as player 3 (any port)
    - 2 PS2 controllers and 2 USB controllers
        - PS2 port 1 controller plays as player 1
        - PS2 port 2 controller plays as player 2
        - USB port 1 controller plays as player 3
        - USB port 2 controller plays as player 4
    - Important Note
        - If you connected PS2 controller on PS2 port 1 and USB controller on USB port (any port) then try playing a single player game USB controller will not work because it will be assigned as player 2 but game expects player 1 to be played so here PS2 controller should be used
        - If you want to play single player game with USB controller then disconnect PS2 controllers and have 1 USB controller connected (any port) then launch the game. So now USB controller will be player 1
 
- **Game Options**
    - Resolutions [ Standard, 720p, 1080i ]
    - Virtual Memory Card (VMC) 
    - PS2RD `.cht` Cheat Engine 
    - Language option for games

- **In Game Reset (IGR)**
    - Hold SELECT for 7–10 seconds while playing the game to return to the PS2 launcher

- **PS2 Launcher Manager [Website]**
    - [PS2-Launcher-Manager](https://irfanlesnar.github.io/PS2-Launcher-Manager/) - You can download game covers here if you don't have internet on PS2


### Download
- **Latest Release:** [Download PS2-Launcher-4.0.0.ELF](https://github.com/Irfanlesnar/PS2-Launcher/releases/download/v4.0.0/PS2-Launcher-4.0.0.ELF)
- **All Releases:** [GitHub Releases](https://github.com/Irfanlesnar/PS2-Launcher/releases)


### Credits
- Project Creator
    - **Irfan Ahamed S** [@irfanmatheena](https://instagram.com/irfanmatheena)

- Based On
    - PS2 Launcher is based on [Open PS2 Loader (OPL)](https://github.com/ps2homebrew/Open-PS2-Loader). Special thanks to the OPL developers and contributors.

- Acknowledgements
    - Game icon images are from [PSBBN Database](https://github.com/CosmicScale/psbbn-art-database)
    - Game logos & background images done by [playnerd.k](https://www.instagram.com/playnerd.k/)
    - Testing for compatibility, and performance are done by
        - playnerd.k / Instagram
        - eliminator1403 / Discord
        - PhilRoll / @PhilRoll
        - J013k / @J013k
        - pkerga / @pkerga
        - MaranelN9 / @MaranelN9
    - Check out everyone helped to make this project at [credits](https://github.com/Irfanlesnar/PS2-Launcher/blob/main/CREDITS.md)

### Disclaimer
This software is intended for use with legally owned games, backups, and homebrew software.


Made with passion for the PlayStation 2 community.

### Donations (Optional) - Helps me to contribute actively
<a href='https://ko-fi.com/P5C821SP5N' target='_blank'><img height='36' style='border:0px;height:36px;' src='https://storage.ko-fi.com/cdn/kofi2.png?v=6' border='0' alt='Buy Me a Coffee at ko-fi.com' /></a>

© Irfan Ahamed S
