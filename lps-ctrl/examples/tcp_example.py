import asyncio
import os

from lps_ctrl import Esp32TcpServer

async def main():
    # 1. 設定總共有幾個 Player
    NUM_PLAYERS = 32
    
    # 2. 設定你的檔案根目錄 
    # (請依據你實際存放各個 Player 資料夾的位置進行修改)
    BASE_DIR = r"C:\Users\yingr\Lightdance2026\ESP32_Advertiser\lps-ctrl\src\lps_ctrl\test_data"
    
    all_control_paths = []
    all_frame_paths = []

    print("正在生成所有 Player 的檔案路徑...")
    
    # 3. 利用迴圈自動生成 1 到 32 號的檔案路徑
    for i in range(1, NUM_PLAYERS + 1):
        # 假設你的資料夾命名規則為 Player_1, Player_2 ... Player_32
        player_dir = os.path.join(BASE_DIR, f"Player_{i}")
        
        control_path = os.path.join(player_dir, "control.dat")
        frame_path = os.path.join(player_dir, "frame.dat")
        
        all_control_paths.append(control_path)
        all_frame_paths.append(frame_path)
        
        # 如果你想看生成的路徑長怎樣，可以把下面這行解除註解：
        # print(f"Player {i} -> {control_path}")

    # 4. 實例化伺服器，並把這兩包 32 個路徑的清單傳進去
    server = Esp32TcpServer(
        control_paths_list=all_control_paths,
        frame_paths_list=all_frame_paths,
        port=3333
    )

    print("\n準備啟動專案伺服器...")
    # 5. 開始執行伺服器邏輯
    await server.start()

if __name__ == '__main__':
    try:
        # asyncio.run() 負責啟動並管理非同步的事件迴圈
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n伺服器已手動關閉。")