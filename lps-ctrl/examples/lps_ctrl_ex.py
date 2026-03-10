from lps_ctrl import ESP32BTSender
import json
import time
PORT = 'COM3' 
def main():    
    try:
        with ESP32BTSender(port=PORT) as sender:
            response = sender.send_burst(cmd_input='PLAY', delay_sec=7, prep_led_sec=3, target_ids=[])
            response = sender.send_burst(cmd_input='PAUSE', delay_sec=8, target_ids=[])
            response = sender.send_burst(cmd_input='CANCEL', delay_sec=1, target_ids=[], data=[1,0,0])
            response = sender.trigger_check()
            response = sender.send_burst(cmd_input='PAUSE', delay_sec=8.5, target_ids=[])
            response = sender.send_burst(cmd_input='TEST', delay_sec=9, target_ids=[1], data=[255,0,0])
            response = sender.send_burst(cmd_input='STOP', delay_sec=10, target_ids=[1])
            response = sender.send_burst(cmd_input='RESET', delay_sec=11, target_ids=[1])            
            time.sleep(2)            
            report = sender.get_latest_report()
            print(json.dumps(report, indent=4, ensure_ascii=False))
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()