import os
import glob
import re
import csv

def main():
    base_dir = '.'
    output_csv = 'mops_results.csv'

    mops_pattern = re.compile(r'Mop/s total\s*=\s*([\d\.]+)')
    dir_pattern_re = re.compile(r'overhead_c8_n1_sp_(\d+)_traces')

    results = []

    # 1. Normale Ordner finden
    search_pattern = os.path.join(base_dir, 'overhead_c8_n1_sp_*_traces')
    directories = glob.glob(search_pattern)

    # 2. Den "plugin_off" Ordner manuell zur Liste hinzufügen
    plugin_off_dir = os.path.join(base_dir, 'overhead_c8_n1_plugin_off')
    if os.path.isdir(plugin_off_dir):
        directories.append(plugin_off_dir)

    for dir_path in directories:
        if os.path.isdir(dir_path):
            dir_name = os.path.basename(dir_path)
            
            # 3. SP-Wert ermitteln: 0 für "plugin_off", sonst aus dem Regex
            if dir_name == 'overhead_c8_n1_plugin_off':
                sp_value = 0
            else:
                sp_match = dir_pattern_re.search(dir_name)
                sp_value = int(sp_match.group(1)) if sp_match else -1
            
            file_pattern = os.path.join(dir_path, 'output_*.txt')
            for file_path in glob.glob(file_pattern):
                file_name = os.path.basename(file_path)
                
                try:
                    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                        for line in f:
                            match = mops_pattern.search(line)
                            if match:
                                mops_val = match.group(1)
                                results.append({
                                    'SP_Value': sp_value,
                                    'Directory': dir_name,
                                    'File': file_name,
                                    'Mops_total': float(mops_val)
                                })
                                break
                except Exception as e:
                    print(f"Fehler beim Lesen von {file_path}: {e}")

    results = sorted(results, key=lambda x: (x['SP_Value'], x['File']))

    with open(output_csv, 'w', newline='', encoding='utf-8') as csvfile:
        fieldnames = ['SP_Value', 'Directory', 'File', 'Mops_total']
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        
        writer.writeheader()
        for row in results:
            writer.writerow(row)

    print(f"Extrahierung abgeschlossen! Es wurden {len(results)} Werte gefunden.")
    print(f"Die Ergebnisse wurden in '{output_csv}' gespeichert.")

if __name__ == '__main__':
    main()
