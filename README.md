# LVM Auto-Extender Lab

This project demonstrates an automated Logical Volume Manager (LVM) extension mechanism on Linux. The system monitors logical volume usage and automatically extends a target logical volume when it reaches a defined usage threshold. The extension is performed by reclaiming space from sibling logical volumes within the same volume group and/or by adding fallback physical volumes when available.

This lab is intended for educational purposes to illustrate LVM administration, monitoring, and automation concepts. It provides hands-on experience with LVM operations, scripting, and system monitoring in a controlled environment.

**Warning:** This lab involves modifying disk partitions and logical volumes. Ensure you are working in a virtual machine or test environment to avoid data loss. Always back up important data before proceeding.

---

## Prerequisites

### Environment Requirements
- A Linux virtual machine (e.g., Ubuntu, CentOS, or similar distribution).
- LVM tools installed (`lvm2` package).
- Build tools: `gcc` and `make`.
- `curl` (optional, for testing network connectivity).

### Block Devices
Two additional virtual disks are required for this lab:
- `/dev/sdb` → Main Physical Volume (PV) for initial setup.
- `/dev/sdc` → Fallback PV (optional, used when additional space is needed).

> **Note:** If your device names differ (e.g., `/dev/vdb`, `/dev/vdc`), update them in the source code (`lvm_manager.c`) and all commands accordingly. You can check available devices using `lsblk` or `fdisk -l`.

---

## Step 1: Set Up Physical Volumes, Volume Group, and Logical Volumes

In this step, we'll initialize the physical volumes, create a volume group, and set up logical volumes with filesystems.

### 1.1 Initialize Physical Volume
Create a physical volume on `/dev/sdb`:

```bash
sudo pvcreate /dev/sdb
```

### 1.2 Create Volume Group
Create a volume group named `vgdata` using the physical volume:

```bash
sudo vgcreate vgdata /dev/sdb
```

### 1.3 Create Logical Volumes
Create three logical volumes within the volume group:

```bash
sudo lvcreate -L 5G -n lv_home vgdata
sudo lvcreate -L 3G -n lv_data1 vgdata
sudo lvcreate -L 3G -n lv_data2 vgdata
```

- `lv_home`: The target volume that will be monitored and extended (5 GB initial size).
- `lv_data1` and `lv_data2`: Donor volumes that can be shrunk to provide space (3 GB each).

### 1.4 Create Filesystems
Format the logical volumes with ext4 filesystems:

```bash
sudo mkfs.ext4 /dev/vgdata/lv_home
sudo mkfs.ext4 /dev/vgdata/lv_data1
sudo mkfs.ext4 /dev/vgdata/lv_data2
```

### 1.5 Mount Logical Volumes
Create mount points and mount the volumes:

```bash
sudo mkdir -p /mnt/lv_home /mnt/lv_data1 /mnt/lv_data2
sudo mount /dev/vgdata/lv_home /mnt/lv_home
sudo mount /dev/vgdata/lv_data1 /mnt/lv_data1
sudo mount /dev/vgdata/lv_data2 /mnt/lv_data2
```

### 1.6 Verify Setup
Check the block devices and mounted filesystems:

```bash
lsblk
df -h
```

You should see the logical volumes mounted at their respective directories with the correct sizes.

---

## Step 2: Configure the LVM Manager

The main program is written in C (`lvm_manager.c`). Open it for configuration:

```bash
nano lvm_manager.c
```

### Key Configuration Parameters

#### DRY_RUN Mode
```c
#define DRY_RUN 1
```
- `1`: Simulation mode (no real LVM changes; logs actions only).
- `0`: Production mode (performs actual shrinking and extending).

#### Thresholds
```c
#define THRESHOLD_PCT 80
#define LOW_PCT 40
```
- `THRESHOLD_PCT`: Usage percentage at which the target LV is considered "hungry" and triggers extension (default: 80%).
- `LOW_PCT`: Usage percentage below which donor LVs are considered over-provisioned (default: 40%).

#### Fallback Physical Volume
```c
static const char *FALLBACK_DEV = "/dev/sdc";
```
- Device path for the fallback PV. If donors don't provide enough space, this PV will be added to the VG.

#### Writer (Load Generator)
The program includes an internal writer thread that simulates disk usage by creating large files in `/mnt/lv_home/writer/`. This helps test the auto-extension logic.

Save and exit the editor after making changes.

---

## Step 3: Build the Program

From the project directory, compile the program:

```bash
make
```

This generates the executable `lvm_manager`. Ensure there are no compilation errors.

---

## Step 4: Run in DRY-RUN Mode (Safe Testing)

For initial testing, enable DRY_RUN mode to simulate operations without making real changes.

1. Ensure `DRY_RUN` is set to `1` in `lvm_manager.c`.
2. Rebuild: `make`
3. Run the program:

```bash
sudo ./lvm_manager
```

### Expected Output
```
LVM Manager starting (DRY_RUN=1)
Monitoring...
```

### Monitoring the Process
Check if the program is running:

```bash
ps aux | grep lvm_manager
```

### Behavior in DRY-RUN Mode
- **Writer Thread**: Creates ~500 MB files every 15 seconds in `/mnt/lv_home/writer/` to simulate usage growth. It stops automatically at 95% usage.
- **Supervisor Reports**:
  - `OVER-PROVISIONED`: For LVs under `LOW_PCT` (potential donors).
  - `HUNGRY`: For LVs above `THRESHOLD_PCT` (logs only, no action in DRY-RUN).

Use this mode to verify monitoring and logging without risking data.

---

## Step 5: Enable Real Shrinking and Extending

Once testing is successful, switch to production mode.

1. Edit `lvm_manager.c`:
   ```c
   #define DRY_RUN 0
   #define THRESHOLD_PCT 80  // Adjust as needed
   ```
2. Rebuild: `make`
3. Run:
   ```bash
   sudo ./lvm_manager
   ```

### Expected Log
```
LVM Manager starting (DRY_RUN=0). Threshold=80%
```

---

## Step 6: Understand the Auto-Extension Workflow

### Trigger Condition
When `/mnt/lv_home` usage exceeds `THRESHOLD_PCT`, the supervisor marks `lv_home` as "HUNGRY" and initiates extension.

### Extension Logic (3-Step Process)

#### Step 1: Check Free Space in Volume Group
- Lists all PVs and available free space in the VG.
- If ≥ 1 GiB free, extend the target LV directly:
  ```bash
  sudo lvextend -r -L +1G /dev/vgdata/lv_home
  ```
  The `-r` flag resizes the filesystem online.

#### Step 2: Shrink Donor Logical Volumes
- Executed if VG has insufficient free space.
- Iterates over sibling LVs (`lv_data1`, `lv_data2`).
- Skips:
  - The target LV (`lv_home`).
  - Any LV mounted at `/mnt/lv_home` (safety check).
- Shrinks ext4 donor LVs with ≥ 1 GiB free space:
  ```bash
  sudo lvreduce -r -L -1G /dev/vgdata/lv_data1
  ```
- Rechecks VG free space and extends the target LV.

#### Step 3: Add Fallback Physical Volume
- If donor shrinking is insufficient:
  ```bash
  sudo pvcreate /dev/sdc
  sudo vgextend vgdata /dev/sdc
  ```
- Then extends the target LV with the new space.
- Filesystem growth uses `resize2fs` for online resizing.

---

## Step 7: Verify Results

After triggering extension, check the changes:

```bash
lvs
df -h
```

### Expected Outcomes
- `lv_home` size increases (e.g., from 5G to 6G).
- Donor LVs (`lv_data1`, `lv_data2`) may decrease in size.
- Writer thread resumes creating files until 95% usage.
- You can restart the program or adjust `WRITER_STOP_PCT` in the source code for further testing.

---

## Step 8: Reset the Lab Environment

To clean up and start over:

### Stop the Program
```bash
sudo pkill lvm_manager
```

### Remove Test Files
```bash
sudo rm -rf /mnt/lv_home/writer/*
```

### Full Reset (Optional)
Unmount and remove all LVs, VG, and PVs:
```bash
sudo umount /mnt/lv_home /mnt/lv_data1 /mnt/lv_data2 2>/dev/null
sudo lvremove -f /dev/vgdata/lv_home /dev/vgdata/lv_data1 /dev/vgdata/lv_data2
sudo vgremove -f vgdata
sudo pvremove -f /dev/sdb /dev/sdc 2>/dev/null
```

Recreate the environment by repeating Step 1.

---

## Author
Ikram Salhi  
December 2024

## Repository
[https://github.com/ikramsalhi/lvm-auto-extender](https://github.com/ikramsalhi/lvm-auto-extender)
