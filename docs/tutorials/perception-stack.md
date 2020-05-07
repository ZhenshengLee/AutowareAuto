Autoware.Auto 3D perception stack {#perception-stack}
============

[TOC]

# Running the Autoware.Auto 3D perception stack

This section leverages the [velodyne_node](https://gitlab.com/autowarefoundation/autoware.auto/AutowareAuto/tree/master/src/drivers/velodyne_node),
which accepts UDP data as an input. Download the sample pcap file containing two LiDAR point clouds
generated by the Velodyne VLP-16 Hi-Res:

- [Dual VLP-16 Hi-Res pcap file](https://drive.google.com/open?id=1vNA009j-tsVVqSeYRCKh_G_tkJQrHvP-)

Place the pcap file within the `adehome` directory, for example `adehome/data/`.

ADE Terminal 1 - start `rviz2`:

```bash
$ ade enter
ade$ rviz2 -d /home/"${USER}"/AutowareAuto/install/autoware_auto_examples/share/autoware_auto_examples/rviz2/autoware.rviz
```
ADE Terminal 2 - start `udpreplay`:

```bash
$ ade enter
ade$ udpreplay ~/data/route_small_loop_rw-127.0.0.1.pcap
```

ADE Terminal 3 - start the `velodyne_node`:

```bash
$ ade enter
ade$ cd AutowareAuto
ade$ source install/setup.bash
ade$ ros2 run velodyne_node velodyne_cloud_node_exe __ns:=/lidar_front __params:=/home/"${USER}"/AutowareAuto/src/drivers/velodyne_node/param/vlp16_test.param.yaml
```

\note
The steps above leverage a pcap file, however the `velodyne_node` can be connected directly to
the sensor. Update the IP address and port arguments in the yaml file to connect to live hardware.

When the `velodyne_node` is running, the resulting LiDAR point cloud can be visualized within `rviz2` as
a `sensor_msgs/PointCloud2` topic type. The data will look similar to the image shown below.

We will now start with the [ray ground filter](../../../src/perception/filters/ray_ground_classifier)
node, for which we will need the Velodyne driver that we ran previously and a pcap capture file
being streamed with `udpreplay`

For this step we will need a fourth ADE terminal, in addition to the previous three:

```bash
$ ade enter
ade$ cd AutowareAuto
ade$ source install/setup.bash
ade$ ros2 run ray_ground_classifier_nodes ray_ground_classifier_cloud_node_exe __params:=/home/"${USER}"/AutowareAuto/src/perception/filters/ray_ground_classifier_nodes/param/vlp16_lexus.param.yaml
```

This will create two new topics (`/nonground_points` and `/points_ground`) that output
`sensor_msgs/PointCloud2`s that we can use to segment the Velodyne point clouds.

With `rviz2` open, we can add visualizations for the two new topics, alternatively an `rviz2`
configuration is provided in `AutowareAuto/src/tools/autoware_auto_examples/rviz2/autoware_ray_ground.rviz`
that can be loaded to automatically set up the visualizations.

![Autoware.Auto ray ground filter snapshot](autoware-auto-ray-ground-filter.png)

Another component in the Autoware.Auto 3D perception stack is the downsampling filter, which is
implemented in the `voxel_grid_nodes` package.
We will run the the voxel grid downsampling node in a new ADE terminal, using the same method as
for the other nodes.

```bash
$ ade enter
ade$ cd AutowareAuto
ade$ source install/setup.bash
ade$ ros2 run voxel_grid_nodes voxel_grid_cloud_node_exe __params:=/home/"${USER}"/AutowareAuto/src/perception/filters/voxel_grid_nodes/param/vlp16_lexus_centroid.param.yaml
```

After this we will have a new topic, named (`/points_downsampled`) that we can visualized with the
provided `rviz2` configuration file in `src/tools/autoware_auto_examples/rviz2/autoware_voxel.rviz`

![Autoware.Auto voxel grid downsampling snapshot](autoware-auto-voxel-grid-downsampling.png)

The next component is in the `euclidean_cluster_nodes` package, to start it we will use ROS 2's launching mechanism to ensure that we have the ray ground classifier node running.

Open a new ADE terminal and type the following:

```bash
$ ade enter
ade$ cd AutowareAuto
ade$ source install/setup.bash
ade$ ros2 launch euclidean_cluster_nodes euclidean_cluster_cloud_node.launch.py
```

You can check that it is running by showing the output of the `/lidar_bounding_boxes` topic:

```bash
ade$ ros2 topic echo /lidar_bounding_boxes
```

Additionally, you can visualize the bounding boxes using rviz2:

```bash
ade$ rviz2 -d /home/"${USER}"/AutowareAuto/install/autoware_auto_examples/share/autoware_auto_examples/rviz2/autoware_bounding_boxes.rviz
```

![Autoware.Auto bounding boxes segmentation snapshot](autoware-auto-bounding-boxes.png)