# Werte, die eingesetzt werden sollen
# cpu_count = 4
# sample_period = 500

import os

cpu_counts = [1,2,3,4,6,8]
sample_periods = [10000000, 5000000, 1000000, 100000, 10000, 1000, 50]

for cpu_count in cpu_counts:
    for sample_period in sample_periods:
        # Pfade zu deinen Dateien
        template_file = "do_npb_test.sh"
        output_file = f"slurm_job_{cpu_count}_{sample_period}.sh"

        # 1. Template-Datei einlesen
        with open(template_file, 'r', encoding='utf-8') as file:
            template_content = file.read()

        # 2. Platzhalter ersetzen
        # Wichtig: SAMPLE_PERIOD taucht im Template zweimal auf, 
        # .replace() tauscht standardmäßig alle Vorkommen aus.
        new_content = template_content.replace("CPU_COUNT", cpu_count)
        new_content = new_content.replace("SAMPLE_PERIOD", sample_period)
        trace_path = f"/data/traces/T_{cpu_counts}_{sample_period}_B"
        new_content = new_content.replace("TRACE_PATH", trace_path)
        os.makedirs(trace_path, exist_ok=True)
        # 3. Neue Datei erstellen und bearbeiteten Text hineinschreiben
        with open(output_file, 'w', encoding='utf-8') as file:
            file.write(new_content)

        print(f"Datei erfolgreich erstellt: {output_file}")




