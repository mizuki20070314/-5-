import time
import random
import json

# シミュレーションしたいデバイスのリスト (MACアドレス)
devices = [
    "AA:AA:AA:AA:AA:01",
    "BB:BB:BB:BB:BB:02",
    "CC:CC:CC:CC:CC:03",
    "DD:DD:DD:DD:DD:04",
    "EE:EE:EE:EE:EE:05"
]

print("Starting Mock Data Generator...")
print("Press Ctrl+C to stop.")

try:
    while True:
        # 1. ランダムにデバイスを1つ選ぶ
        target_mac = random.choice(devices)

        # 2. ランダムなRSSI (電波強度) を生成
        # -30(近い) 〜 -100(遠い) の間でランダムに変動させる
        # 少し揺らぎを持たせる
        rssi = random.randint(-95, -35)

        # 3. JSONデータを作成
        data = {
            "id": target_mac,
            "swing": 0,
            "rssi": rssi,
            "found": 1
        }
        
        json_str = json.dumps(data)

        # 4. ファイルに書き込む
        with open("received_data.txt", "w") as f:
            f.write(json_str)

        print(f"Updated: {json_str}")

        # 5. 少し待つ (0.1秒 〜 0.5秒)
        # 実際の通信のように間隔をバラけさせる
        time.sleep(random.uniform(0.1, 0.5))

except KeyboardInterrupt:
    print("\nStopped.")