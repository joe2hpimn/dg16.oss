# Guide to Setting Up Dual-boot on Windows 10

Refer to [this](https://www.tecmint.com/install-ubuntu-16-04-alongside-with-windows-10-or-8-in-dual-boot/).

You may not be able to shrink the Windows partition sufficiently. Follow these steps to shrink the Windows partition
to around 128GB:

1. Disable Hibernation
```
powercfg /h off
```

This will remove the file `C:\\hiberfil.sys`.

2. Disable Page File

```
Control Panel | Advanced System Settings | [Settings] | [Change]
```

Uncheck `automatic` to disable it.

3. Disable System Restore

Refer to [this](https://www.youtube.com/watch?v=i_H6nXYzZUU).

4. Remove temp files

Run `Disk Cleanup` to remove all temporary files.

5. Defrag

Run `defrag c: /U /V` to squeeze the disk.

Now shrink the Windows partition.

![After shrinkage](https://user-images.githubusercontent.com/145558/27008699-31a15ee6-4e2e-11e7-8416-9a6c0d63e20e.jpg)

When you are done, consider re-enabling the functions you disabled earlier.

### Reboot using the USB Drive

You may need to press `ESC` key to interrupt the booting process and direct the machine to boot from the USB.

### Screen 'Preparing to install Ubuntu'

![Image](https://user-images.githubusercontent.com/145558/27008705-73aa1724-4e2e-11e7-8afe-85d59492e48b.jpg)

### Screen 'Installation type'

![Image](https://user-images.githubusercontent.com/145558/27008707-a2058540-4e2e-11e7-8cdc-3ef65476feba.jpg)

