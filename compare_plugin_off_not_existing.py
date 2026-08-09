import os
import glob
import re
import pandas as pd
import matplotlib.pyplot as plt

def main():
    # Pfad anpassen
    base_dir = 'dir_overhead_c8_n1'
    
    # Zu untersuchende Ordner und ihre Beschriftung
    dirs_to_check = {
        'Plugin Off': 'overhead_c8_n1_plugin_off',
        'Plugin Not Existing': 'overhead_c8_n1_plugin_not_existing'
    }
    
    mops_pattern = re.compile(r'Mop/s total\s*=\s*([\d\.]+)')
    data = []

    # 1. Daten extrahieren
    for label, dir_name in dirs_to_check.items():
        dir_path = os.path.join(base_dir, dir_name)
        file_pattern = os.path.join(dir_path, 'output_*.txt')
        
        files = glob.glob(file_pattern)
        if not files:
            print(f"Warnung: Keine Dateien im Verzeichnis {dir_path} gefunden.")
            
        for file_path in files:
            try:
                with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                    for line in f:
                        match = mops_pattern.search(line)
                        if match:
                            mops_val = float(match.group(1))
                            data.append({
                                'Zustand': label, 
                                'Datei': os.path.basename(file_path),
                                'Mops_total': mops_val
                            })
                            break
            except Exception as e:
                print(f"Fehler bei {file_path}: {e}")

    df = pd.DataFrame(data)
    
    if df.empty:
        print("Keine Mop/s Werte gefunden. Überprüfe die Verzeichnispfade.")
        return

    # KATEGORIEN EXPLIZIT ORDNE N: Verhindert alphabetisches Umlagern durch pandas.boxplot
    categories = list(dirs_to_check.keys())
    df['Zustand'] = pd.Categorical(df['Zustand'], categories=categories, ordered=True)

    # 2. Statistische Auswertung für die Konsole
    stats = df.groupby('Zustand', observed=False)['Mops_total'].agg(['count', 'mean', 'std']).reset_index()
    
    print("\n--- Statistische Auswertung ---")
    print(stats.to_string(index=False))
    print("-------------------------------\n")
    
    # Berechnung des prozentualen Unterschieds
    mean_off = stats.loc[stats['Zustand'] == 'Plugin Off', 'mean'].values[0]
    mean_not_exist = stats.loc[stats['Zustand'] == 'Plugin Not Existing', 'mean'].values[0]
    diff_percent = abs(mean_off - mean_not_exist) / max(mean_off, mean_not_exist) * 100
    
    # 3. Boxplot erstellen
    fig, ax = plt.subplots(figsize=(8, 6))
    
    # Boxplot generieren (nutzt nun die festgelegte Categorical-Reihenfolge)
    df.boxplot(column='Mops_total', by='Zustand', ax=ax, grid=True, 
               boxprops=dict(linewidth=2), medianprops=dict(linewidth=2, color='red'))
    
    # Punkte exakt passend zu den Kategorien zeichnen
    for i, zustand in enumerate(categories):
        y = df[df['Zustand'] == zustand]['Mops_total']
        x = [i + 1] * len(y)  # Position 1, 2, ...
        ax.scatter(x, y, alpha=0.5, color='blue', zorder=3, label='Einzelmessungen' if i == 0 else "")

    plt.title('Performance-Vergleich: Plugin Off vs. Not Existing')
    plt.suptitle('')  # Entfernt den standardmäßigen Pandas-Untertitel
    plt.xlabel('Systemzustand')
    plt.ylabel('Mop/s total')
    
    plt.tight_layout()
    
    plot_name = 'plugin_comparison_boxplot.png'
    plt.savefig(plot_name, dpi=300)
    print(f"Der Plot wurde erfolgreich als '{plot_name}' gespeichert.")

if __name__ == '__main__':
    main()