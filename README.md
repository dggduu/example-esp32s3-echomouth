## 指令查询表
### 清除模拟 efuse表
```sh
esptool.py -p COM4 erase_region 0x12000 0x2000
```