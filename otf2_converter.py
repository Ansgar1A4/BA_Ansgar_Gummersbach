#!/usr/bin/env python3
import sys
import json
import otf2

if len(sys.argv) < 3:
    print("Usage: python3 otf2_converter.py <input_otf2_file> <output_json_file>")
    sys.exit(1)

input_file = sys.argv[1]
output_file = sys.argv[2]

events = []

def get_name(definition):
    if hasattr(definition, 'name'):
        return str(definition.name)
    return str(definition)

with otf2.reader.open(input_file) as reader:
    # Definitionen vorladen
    for def_type in dir(reader.definitions):
        if not def_type.startswith('_'):
            try:
                registry = getattr(reader.definitions, def_type)
                if hasattr(registry, '__iter__'):
                    for _ in registry: pass
            except Exception: pass

    definitions = reader.definitions
    
    for location, event in reader.events:
        # Wir holen uns den exakten Klassennamen des Events (z.B. "MpiSend", "Metric", etc.)
        event_class_name = event.__class__.__name__
        
        # Zeitstempel konvertieren
        ts = event.time / 1000.0
        pid = get_name(location.group) if location.group else "Process"
        tid = get_name(location)
        
        # Versuchen, einen sinnvollen Namen für die Anzeige zu finden
        name = event_class_name
        if hasattr(event, 'region'):
            try:
                name = f"{event_class_name}: {get_name(definitions.regions[event.region])}"
            except KeyError: pass
        elif hasattr(event, 'calling_context'):
            try:
                cctx = definitions.calling_contexts[event.calling_context]
                name = f"{event_class_name}: {get_name(definitions.regions[cctx.region])}"
            except KeyError: pass

        # Bestimmen, ob es ein Start- oder End-Event ist, oder ein "Instant"-Event (Punkt auf der Timeline)
        ph = "I" # Instant / Info-Punkt als Default
        if "Enter" in event_class_name or "Begin" in event_class_name:
            ph = "B"
        elif "Leave" in event_class_name or "End" in event_class_name:
            ph = "E"

        # Event-Metadaten für Chrome/Perfetto zusammenbauen
        event_data = {
            "name": name,
            "ph": ph,
            "ts": ts,
            "pid": pid,
            "tid": tid,
            "args": {"otf2_type": event_class_name}
        }
        
        # Falls es ein Metric-Event ist, den Wert mitspeichern
        if hasattr(event, 'value'):
            event_data["args"]["value"] = event.value
            
        events.append(event_data)

# Sortieren
events.sort(key=lambda x: x["ts"])

with open(output_file, 'w') as f:
    json.dump(events, f, indent=2)

print(f"Konvertierung abgeschlossen! {len(events)} Events jeglichen Typs extrahiert.")