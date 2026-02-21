#!/usr/bin/env bash
set -euo pipefail

DEV_SYS="${DEV_SYS:-/sys/bus/iio/devices/iio:device0}"
DEV_NODE="${DEV_NODE:-/dev/iio:device0}"

# 选择 buffer 接口：优先 buffer0，其次 buffer
if [[ -d "${DEV_SYS}/buffer0" ]]; then
  BUF_DIR="${DEV_SYS}/buffer0"
elif [[ -d "${DEV_SYS}/buffer" ]]; then
  BUF_DIR="${DEV_SYS}/buffer"
else
  echo "[ERR] No buffer directory found under ${DEV_SYS}" >&2
  exit 1
fi

TRIG_CUR="${DEV_SYS}/trigger/current_trigger"

need_root() {
  if [[ "${EUID}" -ne 0 ]]; then
    echo "[ERR] Please run as root: sudo $0 $*" >&2
    exit 1
  fi
}

sysfs_write() {
  local path="$1"
  local val="$2"
  if [[ ! -e "$path" ]]; then
    echo "[ERR] Missing sysfs node: $path" >&2
    exit 1
  fi
  echo "$val" > "$path"
}

sysfs_read() {
  local path="$1"
  [[ -e "$path" ]] && cat "$path" || true
}

find_trigger_name() {
  # 优先：bmi270-trig-1
  if [[ -d /sys/bus/iio/devices/trigger0 ]] && [[ "$(cat /sys/bus/iio/devices/trigger0/name 2>/dev/null || true)" == "bmi270-trig-1" ]]; then
    echo "bmi270-trig-1"
    return
  fi

  # 其次：在 /sys/bus/iio/devices/trigger* 中找第一个 name
  for t in /sys/bus/iio/devices/trigger*; do
    [[ -d "$t" ]] || continue
    if [[ -f "$t/name" ]]; then
      local n
      n="$(cat "$t/name" 2>/dev/null || true)"
      if [[ -n "$n" ]]; then
        echo "$n"
        return
      fi
    fi
  done

  echo ""
}

disable_buffer() {
  # 关闭 buffer（忽略错误）
  [[ -f "${BUF_DIR}/enable" ]] && echo 0 > "${BUF_DIR}/enable" 2>/dev/null || true
  [[ -f "${DEV_SYS}/buffer/enable" ]] && echo 0 > "${DEV_SYS}/buffer/enable" 2>/dev/null || true
  [[ -f "${DEV_SYS}/buffer0/enable" ]] && echo 0 > "${DEV_SYS}/buffer0/enable" 2>/dev/null || true
}

disable_all_channels() {
  # 关闭 scan_elements 下所有 *_en
  if [[ -d "${DEV_SYS}/scan_elements" ]]; then
    for f in "${DEV_SYS}/scan_elements/"*_en; do
      [[ -e "$f" ]] || continue
      echo 0 > "$f" 2>/dev/null || true
    done
  fi

  # 你的系统里 buffer0 也挂了 *_en（镜像），存在就顺手关掉
  if [[ -d "${DEV_SYS}/buffer0" ]]; then
    shopt -s nullglob
    local arr=( "${DEV_SYS}/buffer0/"*_en )
    shopt -u nullglob
    for f in "${arr[@]}"; do
      [[ -e "$f" ]] || continue
      echo 0 > "$f" 2>/dev/null || true
    done
  fi
}

enable_needed_channels() {
  # 统一从 scan_elements 下开（最通用）
  local SE="${DEV_SYS}/scan_elements"
  if [[ ! -d "$SE" ]]; then
    echo "[ERR] Missing scan_elements: $SE" >&2
    exit 1
  fi

  sysfs_write "$SE/in_accel_x_en" 1
  sysfs_write "$SE/in_accel_y_en" 1
  sysfs_write "$SE/in_accel_z_en" 1

  sysfs_write "$SE/in_anglvel_x_en" 1
  sysfs_write "$SE/in_anglvel_y_en" 1
  sysfs_write "$SE/in_anglvel_z_en" 1

  if [[ -f "$SE/in_timestamp_en" ]]; then
    sysfs_write "$SE/in_timestamp_en" 1
  else
    echo "[WARN] No in_timestamp_en found; dt will be less accurate." >&2
  fi
}

set_odr_if_requested() {
  # 环境变量可选：ACC_HZ / GYR_HZ
  if [[ -n "${ACC_HZ:-}" ]]; then
    if [[ -f "${DEV_SYS}/in_accel_sampling_frequency" ]]; then
      echo "[INFO] Set accel ODR: ${ACC_HZ} Hz"
      sysfs_write "${DEV_SYS}/in_accel_sampling_frequency" "${ACC_HZ}"
    else
      echo "[WARN] No accel sampling_frequency node" >&2
    fi
  fi

  if [[ -n "${GYR_HZ:-}" ]]; then
    if [[ -f "${DEV_SYS}/in_anglvel_sampling_frequency" ]]; then
      echo "[INFO] Set gyro ODR: ${GYR_HZ} Hz"
      sysfs_write "${DEV_SYS}/in_anglvel_sampling_frequency" "${GYR_HZ}"
    else
      echo "[WARN] No gyro sampling_frequency node" >&2
    fi
  fi
}

bind_trigger() {
  local trig
  trig="$(find_trigger_name)"
  if [[ -z "$trig" ]]; then
    echo "[ERR] No IIO trigger found under /sys/bus/iio/devices/trigger*" >&2
    exit 1
  fi

  echo "[INFO] Bind trigger: ${trig}"
  sysfs_write "$TRIG_CUR" "$trig"

  local cur
  cur="$(sysfs_read "$TRIG_CUR")"
  if [[ "$cur" != "$trig" ]]; then
    echo "[ERR] Failed to bind trigger. current_trigger='$cur'" >&2
    exit 1
  fi
}

configure_buffer() {
  local len="${BUF_LEN:-256}"
  local wm="${BUF_WATERMARK:-1}"

  [[ -f "${BUF_DIR}/length" ]] && { echo "[INFO] buffer length=${len}"; sysfs_write "${BUF_DIR}/length" "$len"; }
  [[ -f "${BUF_DIR}/watermark" ]] && { echo "[INFO] buffer watermark=${wm}"; sysfs_write "${BUF_DIR}/watermark" "$wm"; }
}

enable_buffer() {
  echo "[INFO] Enable buffer: ${BUF_DIR}/enable"
  sysfs_write "${BUF_DIR}/enable" 1
  local en
  en="$(sysfs_read "${BUF_DIR}/enable")"
  if [[ "$en" != "1" ]]; then
    echo "[ERR] Buffer enable did not stick (enable='$en')" >&2
    exit 1
  fi
}

status() {
  echo "=== IIO device ==="
  echo "DEV_SYS=${DEV_SYS}"
  echo "DEV_NODE=${DEV_NODE}"
  echo "BUF_DIR=${BUF_DIR}"
  echo -n "trigger/current_trigger="; sysfs_read "$TRIG_CUR" | tr -d '\n'; echo
  echo -n "buffer/enable="; sysfs_read "${BUF_DIR}/enable" | tr -d '\n'; echo
  echo -n "buffer/length="; sysfs_read "${BUF_DIR}/length" | tr -d '\n'; echo
  echo -n "buffer/watermark="; sysfs_read "${BUF_DIR}/watermark" | tr -d '\n'; echo

  echo "=== channels enabled (scan_elements) ==="
  [[ -d "${DEV_SYS}/scan_elements" ]] && grep -H . "${DEV_SYS}/scan_elements/"*_en 2>/dev/null || true

  echo "=== ODR ==="
  echo -n "accel="; sysfs_read "${DEV_SYS}/in_accel_sampling_frequency" | tr -d '\n'; echo " Hz"
  echo -n "gyro=";  sysfs_read "${DEV_SYS}/in_anglvel_sampling_frequency" | tr -d '\n'; echo " Hz"
}

dump_hex() {
  local n="${1:-128}"
  if [[ ! -e "${DEV_NODE}" ]]; then
    echo "[ERR] Missing device node: ${DEV_NODE}" >&2
    exit 1
  fi
  echo "[INFO] hexdump -n ${n} ${DEV_NODE}"
  hexdump -C -n "${n}" "${DEV_NODE}"
}

start() {
  need_root "$@"
  echo "[INFO] STOP buffer first"
  disable_buffer

  echo "[INFO] Configure ODR (optional)"
  set_odr_if_requested

  echo "[INFO] Disable all channels"
  disable_all_channels

  echo "[INFO] Enable required channels: accel xyz + gyro xyz + timestamp"
  enable_needed_channels

  echo "[INFO] Bind trigger"
  bind_trigger

  echo "[INFO] Configure buffer (length/watermark)"
  configure_buffer

  enable_buffer

  echo "[OK] Buffer started. Read data from: ${DEV_NODE}"
  echo "Tip: sudo $0 dump 256"
}

stop() {
  need_root "$@"
  disable_buffer
  echo "[OK] Buffer stopped."
}

usage() {
  cat <<EOF
Usage:
  sudo $0 start
  sudo $0 stop
  $0 status
  sudo $0 dump [Nbytes]

Environment variables:
  DEV_SYS=/sys/bus/iio/devices/iio:device0
  DEV_NODE=/dev/iio:device0
  ACC_HZ=100          (optional)
  GYR_HZ=200          (optional)
  BUF_LEN=256         (optional)
  BUF_WATERMARK=1     (optional)

Examples:
  sudo ACC_HZ=100 GYR_HZ=200 BUF_LEN=512 BUF_WATERMARK=1 $0 start
  $0 status
  sudo $0 dump 128
  sudo $0 stop
EOF
}

cmd="${1:-}"
case "$cmd" in
  start) shift; start "$@";;
  stop) shift; stop "$@";;
  status) shift; status;;
  dump) shift; need_root "$@"; dump_hex "${1:-128}";;
  *) usage; exit 1;;
esac
