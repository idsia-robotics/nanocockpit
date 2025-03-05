# Streamer clients

Clients to connect to the camera+state streamer running on a Crazyflie.

## Matplotlib GUI client

Install the client as a Python package:

```shell
$ pip install src/client/aideck_cpx_streamer
```

<!-- The `-e` flag allows you to edit files in your package without having to re-install it every time. -->

Launch a minimal client that shows the received data in a GUI window:

```shell
$ plt_viewer
```

By default the client will attempt to connect to `aideck.local`, the default mDNS hostname used by the NINA code. You can connect to a different hostname using:

```shell
$ plt_viewer -host your-hostname.local
```

This client also supports saving the received data in a simple dataset format:

```shell
$ plt_viewer -save dataset/
```

The resulting `dataset/` directory will contain one PNG image for each received camera frame and a `metadata.csv` containing the onboard state estimation corresponding to each frame, plus some extra information.

## ROS2 client

To install the ROS client, first set up a Colcon workspace if you don't have one already (e.g., in `~/dev_ws`):
https://docs.ros.org/en/humble/Tutorials/Beginner-Client-Libraries/Creating-A-Workspace/Creating-A-Workspace.html

Then, clone the entire repository inside the `src` directory of your workspace:

```shell
$ cd ~/dev_ws/src
$ git clone https://github.com/idsia-robotics/crazyflie-nanocockpit.git
```

Build your workspace and activate it:

```shell
$ cd ..
$ colcon build --symlink-install
$ source install/setup.zsh
```

Building the workspace with `--symlink-install` allows you to edit (some) files in your packages without having to rebuild the workspace every time.

Launch the client:

```shell
$ ros2 launch aideck_cpx_streamer ros_viewer_launch.xml
```

As before, custom hostnames can be specified with the `host` parameter:

```shell
$ ros2 launch aideck_cpx_streamer ros_viewer_launch.xml host:=your-hostname.local
```
