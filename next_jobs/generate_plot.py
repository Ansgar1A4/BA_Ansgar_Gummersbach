import pandas as pd
import matplotlib.pyplot as plt

def main():
    csv_file = 'mops_results.csv'
    
    try:
        df = pd.read_csv(csv_file)
    except FileNotFoundError:
        print(f"Fehler: Die Datei {csv_file} wurde nicht gefunden.")
        return

    # 1. 'min' und 'max' zur statistischen Auswertung hinzufügen
    stats = df.groupby('SP_Value')['Mops_total'].agg(['count', 'mean', 'std', 'min', 'max']).reset_index()
    stats['std'] = stats['std'].fillna(0)

    # Konsolenausgabe der Rohdaten-Statistik
    print("\n--- Statistische Auswertung nach SP_Value ---")
    print(stats.to_string(index=False))
    print("---------------------------------------------\n")

    # Teile die Daten in Baseline (SP = 0) und reguläre Traces (SP > 0)
    baseline_data = stats[stats['SP_Value'] == 0]
    trace_data = stats[stats['SP_Value'] > 0].copy()

    # 2. Automatische Overhead-Berechnung gegenüber der Baseline
    if not baseline_data.empty:
        base_mean = baseline_data['mean'].values[0]
        
        # Performance-Verlust (Overhead) in % berechnen:
        trace_data['Overhead (%)'] = ((base_mean - trace_data['mean']) / base_mean) * 100
        
        print("--- Performance-Overhead im Vergleich zur Baseline (SP=0) ---")
        print(trace_data[['SP_Value', 'count', 'mean', 'min', 'max', 'Overhead (%)']].to_string(index=False))
        print("-------------------------------------------------------------\n")

    plt.figure(figsize=(10, 6))

    # 3. Asymmetrische Fehlerbalken für Min/Max berechnen
    # Abstand_unten = Mittelwert - Minimum
    # Abstand_oben = Maximum - Mittelwert
    lower_error = trace_data['mean'] - trace_data['min']
    upper_error = trace_data['max'] - trace_data['mean']
    asymmetric_error = [lower_error, upper_error]

    # Zeichne die regulären SP-Daten (jetzt mit Min/Max)
    plt.errorbar(
        x=trace_data['SP_Value'], 
        y=trace_data['mean'], 
        yerr=asymmetric_error, 
        fmt='-o', capsize=5, capthick=2, ecolor='red', color='blue', markersize=6,
        label='Plugin On (Mittelwert mit Min/Max-Spanne)'
    )

    # 4. Zeichne die "Plugin Off" Baseline als horizontale Linie
    if not baseline_data.empty:
        base_mean = baseline_data['mean'].values[0]
        base_min = baseline_data['min'].values[0]
        base_max = baseline_data['max'].values[0]
        
        # Horizontale Linie für den Mittelwert
        plt.axhline(y=base_mean, color='green', linestyle='--', label='Baseline (Plugin Off)')
        
        # Schattierter Bereich für die Min/Max-Spanne der Baseline
        plt.fill_between(
            trace_data['SP_Value'], 
            base_min, 
            base_max, 
            color='green', alpha=0.15,
            label='Baseline Streuung (Min/Max)'
        )

    plt.xscale('log')
    plt.xlabel('Sample-Period')
    plt.ylabel('Mop/s total')
    
    plt.grid(True, which="both", ls="--", alpha=0.5)
    plt.legend()
    plt.tight_layout()

    plt.savefig('mops_minmax_plot.png', dpi=300)
    print("Plot mit Min/Max-Ausreißern wurde erstellt!")

if __name__ == '__main__':
    main()