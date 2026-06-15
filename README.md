# Shot
A minimal xlib screenshot tool written in C

#### plan
Gonna Add Window Screenshot from [scr2ppm](https://github.com/igorlogius/scr2ppm) (OPTIONAL)

## Config
Config is in the first few lines of `config.h`

## Compiling
Requires `xlib`, `xclip` and `libwebp`  
Use `make` to compile  
Use `make debug` to compile with debug info

## Installing
Run `make install` to install  
Run `make uninstall` to uninstall  

## Usage
Run `shot` to select an area manually 

You can use `f` to select the fullscreen.
After selection, you can use `w` to save, `y` to copy, `a` to open your annotation app.
Scripts are binded to `1-9` alphabetically on the `~/.config/screenshot/exec`.

Run `shot 1` to screenshot the whole screen


