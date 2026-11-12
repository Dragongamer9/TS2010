# Getting Started with Trainz Route Viewer

This is a viewer that lets you look at the routes and trains from Trainz 2010 on your computer. It is **not** the actual game—you can’t drive the trains yet, but you can fly around and see all the routes, scenery and trains.

## Before you start

You need to install some building tools first. Open the terminal and run:

```sh
sudo apt update
sudo apt install cmake ninja-build g++ libsdl2-dev libgl-dev python3 pkg-config
```

## Building the viewer

In the project folder, run:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Installing it

Install it so you can start it from the menu:

```sh
cmake --install build --prefix ~/.local
```

## Tell it where Trainz 2010 is

Run this once, but change `/path/to/TS2010` to the actual folder where your Trainz 2010 is installed:

```sh
trainz-route-viewer --set-install /path/to/TS2010
```

## Using the viewer

Find **Trainz Route Viewer (TS2010 port)** in your applications menu and click it. A terminal window will open, showing your routes and sessions. Pick one, and a new window will open where you can fly around and look at the route.

The viewer uses its own renderer, so expect roughly 5–15 frames per second. It’s for checking what the port reads, not for playing.

## Controls

**Keyboard:**
- W or Up arrow – move forward
- S or Down arrow – move backward
- A – move left
- D – move right
- Left arrow – turn left
- Right arrow – turn right
- Page Up – look up
- Page Down – look down
- Q – move down
- E – move up
- Hold Shift – move 5× faster
- R – back to the start view
- Esc – quit

**Mouse:**
- Right-click and drag – look around
- Scroll wheel – move forward/back (10 m a step)

**Gamepad:**
- Left stick – move
- Right stick – look
- Left trigger – move down
- Right trigger – move up
- Hold A button – move 5× faster
- Start button – back to the start view
