## 从 Bosch 的 BMI270 SensorAPI 提取配置并生成 fw（Windows 生成 + VS Code SSH 部署）

Bosch 的 BMI270 SensorAPI 仓库里包含初始化所需的“配置数据”（也就是驱动要的 init data）。

**思路：** 在 Windows 环境下提取 `bmi270.c` 里的 config 数组，生成二进制文件 `bmi270-init-data.fw`，然后通过 VS Code SSH 直接将文件拖拽上传到树莓派，并配置到 `/lib/firmware/` 目录中。

### 1) 在 Windows 上拉取 SensorAPI 仓库

打开你的 Windows 终端（PowerShell 或 CMD），进入你的工作目录并拉取代码：

Bash

```
git clone https://github.com/boschsensortec/BMI270_SensorAPI.git
```

### 2) 在 Windows 上用脚本导出 fw 文件

将以下脚本保存为 `extract_fw.py`（放在与刚刚 clone 下来的文件夹同级的目录中）：

Python

```
#!/usr/bin/env python3
import re
import sys
from pathlib import Path

def find_file(name: str) -> Path:
    # 在当前目录递归寻找文件
    matches = list(Path(".").rglob(name))
    if not matches:
        raise FileNotFoundError(f"Cannot find {name} under {Path('.').resolve()}")
    # 优先选择路径里包含 SensorAPI 的那个（如果有）
    matches.sort(key=lambda p: (0 if "SensorAPI" in str(p) else 1, len(str(p))))
    return matches[0]

def extract_array_bytes(c_text: str) -> bytes:
    # 尝试匹配常见的配置数组命名（不同版本可能不同）
    patterns = [
        r'(?:const\s+)?uint8_t\s+(bmi270_[a-zA-Z0-9_]*config[a-zA-Z0-9_]*)\s*\[\s*\]\s*=\s*\{([^}]*)\};',
        r'(?:const\s+)?uint8_t\s+(bmi270_[a-zA-Z0-9_]*config[a-zA-Z0-9_]*)\s*\[\s*\]\s*=\s*\{(.*?)\};',
    ]
    for pat in patterns:
        m = re.search(pat, c_text, re.S)
        if m:
            body = m.group(2)
            nums = re.findall(r'0x[0-9a-fA-F]+|\b\d+\b', body)
            data = bytes(int(x, 0) & 0xFF for x in nums)
            return data
    return b""

def main():
    try:
        bmi270_c = find_file("bmi270.c")
    except FileNotFoundError as e:
        print(f"ERROR: {e}")
        sys.exit(1)

    text = bmi270_c.read_text(errors="ignore")
    data = extract_array_bytes(text)
    if not data:
        print(f"ERROR: found {bmi270_c} but failed to locate config array inside it.")
        print("Tip: open bmi270.c and search for 'config' or 'bmi270_config_file'.")
        sys.exit(1)

    out = Path("bmi270-init-data.fw")
    out.write_bytes(data)
    print(f"OK: wrote {out} ({len(data)} bytes) from {bmi270_c}")

if __name__ == "__main__":
    main()
```

在 Windows 终端运行该脚本：

Bash

```
python extract_fw.py
```

运行成功后，当前目录下会生成 `bmi270-init-data.fw` 文件。

### 3) 通过 VS Code SSH 上传到树莓派

1. 在 VS Code 中，通过 Remote-SSH 连接到你的树莓派。
2. 在左侧资源管理器中，打开树莓派上的工作目录（例如 `~/linux_driver_learning/04_bmi270_i2c`）。
3. 将 Windows 目录中刚刚生成的 `bmi270-init-data.fw` 文件，**直接拖拽**到 VS Code 的资源管理器面板中，即可完成上传。

### 4) 在树莓派上安装并验证

在 VS Code 底部打开树莓派的集成终端（Terminal），执行以下命令将固件安装到系统目录：

Bash

```
cd ~/linux_driver_learning/04_bmi270_i2c  # 确保处于你上传固件的目录
sudo install -m 0644 bmi270-init-data.fw /lib/firmware/bmi270-init-data.fw
sync
```

重新加载驱动并查看内核日志验证：

Bash

```
sudo modprobe -r bmi270_i2c bmi270_core 2>/dev/null || true
sudo modprobe bmi270_i2c
dmesg -T | egrep -i "bmi270|firmware|init-data|iio" | tail -200
```

如果成功，你应该能看到 probe 继续往下走（不再提示 “Failed to load init data file”）。