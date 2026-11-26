"""
D2.GreenHouse Stand - PC program (Python 3, tkinter + matplotlib + pyserial).

Програма керування навчально-демонстраційним стендом D2.GreenHouse.
    python d2gh_stand.py            (інтерфейс українською)
    python d2gh_stand.py --lang en  (English UI)

Tabs: Monitoring | Outputs | Parameters | Bus & log | Faults | Lab work
"""
import argparse
import csv
import datetime as dt
import glob
import json
import os
import queue
import time
import tkinter as tk
from tkinter import ttk, messagebox, filedialog, colorchooser

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg

from stand_protocol import StandLink, list_ports, parse_kv, decode_errors, FAULTS

HERE = os.path.dirname(os.path.abspath(__file__))

TXT = {
 "uk": {"title": "Стенд D2.GreenHouse – програма керування", "port": "Порт:", "refresh": "Оновити",
        "connect": "Підключити", "disconnect": "Відключити", "mode": "Режим:", "auto": "АВТО",
        "manual": "РУЧНИЙ", "stop": "СТОП", "period": "Період, мс:", "rec": "Запис CSV", "recstop": "Зупинити запис",
        "t_mon": "Моніторинг", "t_out": "Керування виходами", "t_par": "Параметри", "t_bus": "Шина і журнал",
        "t_flt": "Несправності", "t_lab": "Лабораторна робота", "value": "Значення", "name": "Величина",
        "unit": "Од.", "read": "Прочитати", "set": "Записати", "save": "Зберегти в EEPROM",
        "defaults": "Типові значення", "send": "Надіслати", "mon485": "Монітор RS-485", "monspi": "Монітор SPI (F030)",
        "clear": "Очистити", "errors": "Слово помилок:", "noerr": "помилок немає", "lab": "Робота:",
        "start": "Почати", "next": "Далі", "prev": "Назад", "notes": "Нотатки / висновки:",
        "report": "Зберегти звіт", "manual_only": "Керування виходами доступне в режимі РУЧНИЙ",
        "color": "Колір стрічки WS2812", "beep": "Сигнал 200 мс", "notconn": "Немає з’єднання",
        "fault_hint": "Імітація несправності: прошивка поводиться так, ніби датчик/вузол несправний.",
        "relays": ["K1 нагрівач повітря", "K2 насос", "K3 підігрів ґрунту", "K4 УФ-лампа", "K5 досвічування"],
        "pwms": ["PW1 вентилятор", "PW2 LED-освітлення", "PW3 резерв", "PW4 резерв"]},
 "en": {"title": "D2.GreenHouse Stand – control program", "port": "Port:", "refresh": "Refresh",
        "connect": "Connect", "disconnect": "Disconnect", "mode": "Mode:", "auto": "AUTO",
        "manual": "MANUAL", "stop": "STOP", "period": "Period, ms:", "rec": "Record CSV", "recstop": "Stop recording",
        "t_mon": "Monitoring", "t_out": "Outputs", "t_par": "Parameters", "t_bus": "Bus & log",
        "t_flt": "Faults", "t_lab": "Lab work", "value": "Value", "name": "Quantity",
        "unit": "Unit", "read": "Read", "set": "Write", "save": "Save to EEPROM",
        "defaults": "Defaults", "send": "Send", "mon485": "RS-485 monitor", "monspi": "SPI monitor (F030)",
        "clear": "Clear", "errors": "Error word:", "noerr": "no errors", "lab": "Lab:",
        "start": "Start", "next": "Next", "prev": "Back", "notes": "Notes / conclusions:",
        "report": "Save report", "manual_only": "Outputs can be controlled in MANUAL mode",
        "color": "WS2812 strip colour", "beep": "Beep 200 ms", "notconn": "Not connected",
        "fault_hint": "Fault injection: the firmware behaves as if the sensor/node were faulty.",
        "relays": ["K1 air heater", "K2 pump", "K3 soil heater", "K4 UV lamp", "K5 grow light"],
        "pwms": ["PW1 fan", "PW2 LED light", "PW3 spare", "PW4 spare"]},
}

# quantities shown in the monitoring table: key, Ukrainian name, English name, unit
QUANT = [("T1", "Температура повітря", "Air temperature", "°C"),
         ("T2", "Температура ґрунту", "Soil temperature", "°C"),
         ("T3", "Температура води", "Water temperature", "°C"),
         ("TA", "Температура (AM2301A)", "Temperature (AM2301A)", "°C"),
         ("RH", "Відносна вологість", "Relative humidity", "%"),
         ("W1", "Вологість ґрунту 1", "Soil moisture 1", "%"),
         ("W2", "Вологість ґрунту 2", "Soil moisture 2", "%"),
         ("LIGHT", "Освітленість", "Light level", "%"),
         ("UV", "УФ-сигнал", "UV signal", "mV"),
         ("CO2", "CO₂", "CO₂", "ppm"),
         ("LVL", "Вода вище MIN", "Water above MIN", ""),
         ("FULL", "Бак повний", "Tank full", ""),
         ("K", "Реле K1..K5", "Relays K1..K5", ""),
         ("P1", "Вентилятор PW1", "Fan PW1", "%"),
         ("P2", "LED PW2", "LED PW2", "%"),
         ("WAT", "Стан поливу (0/1/2)", "Watering state (0/1/2)", ""),
         ("UVD", "Доза УФ", "UV dose", "u"),
         ("mode", "Режим", "Mode", ""),
         ("time", "Час стенда", "Stand time", "")]

HISTORY_S = 600
INT_KEYS = {"LVL", "FULL", "WAT", "CO2", "UV", "P1", "P2"}            # chart window, seconds


class App(tk.Tk):
    def __init__(self, lang):
        super().__init__()
        self.lang = lang
        self.t = TXT[lang]
        self.title(self.t["title"])
        self.geometry("1180x760")
        self.link = StandLink()
        self.hist = {k: [] for k in ("t", "T1", "T2", "TA", "RH", "W1", "W2", "P1", "K1")}
        self.last = {}
        self.params = {}
        self.csv_file = None
        self.csv_writer = None
        self.lab = None
        self.lab_step = 0
        self._build()
        self.after(50, self._poll)

    # ------------------------------------------------------------ layout
    def _build(self):
        t = self.t
        top = ttk.Frame(self, padding=4)
        top.pack(fill="x")
        ttk.Label(top, text=t["port"]).pack(side="left")
        self.port = ttk.Combobox(top, width=22, values=list_ports())
        self.port.pack(side="left", padx=2)
        if self.port["values"]:
            self.port.current(0)
        ttk.Button(top, text=t["refresh"], command=lambda: self.port.configure(values=list_ports())).pack(side="left")
        self.btn_conn = ttk.Button(top, text=t["connect"], command=self.toggle_connect)
        self.btn_conn.pack(side="left", padx=6)
        ttk.Label(top, text=t["mode"]).pack(side="left", padx=(12, 2))
        self.mode = tk.StringVar(value="AUTO")
        ttk.Radiobutton(top, text=t["auto"], value="AUTO", variable=self.mode,
                        command=lambda: self.cmd("MODE AUTO")).pack(side="left")
        ttk.Radiobutton(top, text=t["manual"], value="MANUAL", variable=self.mode,
                        command=lambda: self.cmd("MODE MANUAL")).pack(side="left")
        tk.Button(top, text=t["stop"], bg="#c62828", fg="white", width=8,
                  command=lambda: self.cmd("STOP")).pack(side="left", padx=10)
        ttk.Label(top, text=t["period"]).pack(side="left")
        self.period = ttk.Combobox(top, width=6, values=["200", "500", "1000", "2000", "5000"])
        self.period.set("1000")
        self.period.pack(side="left")
        self.period.bind("<<ComboboxSelected>>", lambda e: self.cmd("STREAM " + self.period.get()))
        self.btn_rec = ttk.Button(top, text=t["rec"], command=self.toggle_csv)
        self.btn_rec.pack(side="left", padx=8)
        self.status = ttk.Label(top, text=t["notconn"], foreground="#666")
        self.status.pack(side="right")

        self.err = tk.Label(self, text=t["errors"] + " –", anchor="w", bg="#eef3f9", padx=6)
        self.err.pack(fill="x")

        nb = ttk.Notebook(self)
        nb.pack(fill="both", expand=True)
        self.nb = nb
        self._tab_monitor(nb)
        self._tab_outputs(nb)
        self._tab_params(nb)
        self._tab_bus(nb)
        self._tab_faults(nb)
        self._tab_lab(nb)

    def _tab_monitor(self, nb):
        f = ttk.Frame(nb, padding=4)
        nb.add(f, text=self.t["t_mon"])
        self.tv = ttk.Treeview(f, columns=("v", "u"), height=20)
        self.tv.heading("#0", text=self.t["name"])
        self.tv.heading("v", text=self.t["value"])
        self.tv.heading("u", text=self.t["unit"])
        self.tv.column("#0", width=210)
        self.tv.column("v", width=150, anchor="e")
        self.tv.column("u", width=50)
        for key, uk, en, unit in QUANT:
            self.tv.insert("", "end", iid=key, text=uk if self.lang == "uk" else en, values=("–", unit))
        self.tv.pack(side="left", fill="y")
        self.fig = Figure(figsize=(7.6, 6.2), dpi=96)
        self.ax1 = self.fig.add_subplot(311)
        self.ax2 = self.fig.add_subplot(312, sharex=self.ax1)
        self.ax3 = self.fig.add_subplot(313, sharex=self.ax1)
        self.fig.subplots_adjust(left=0.09, right=0.98, top=0.97, bottom=0.07, hspace=0.25)
        self.canvas = FigureCanvasTkAgg(self.fig, master=f)
        self.canvas.get_tk_widget().pack(side="left", fill="both", expand=True)

    def _tab_outputs(self, nb):
        f = ttk.Frame(nb, padding=10)
        nb.add(f, text=self.t["t_out"])
        ttk.Label(f, text=self.t["manual_only"], foreground="#a15c00").grid(row=0, column=0, columnspan=3, sticky="w")
        self.relay_btn = []
        for i, name in enumerate(self.t["relays"]):
            b = tk.Button(f, text=name + ": OFF", width=28, bg="#e0e0e0",
                          command=lambda k=i: self.toggle_relay(k))
            b.grid(row=1 + i, column=0, sticky="w", pady=3)
            self.relay_btn.append(b)
        self.pwm_scale = []
        for i, name in enumerate(self.t["pwms"]):
            ttk.Label(f, text=name).grid(row=1 + i, column=1, sticky="e", padx=(30, 6))
            s = tk.Scale(f, from_=0, to=100, orient="horizontal", length=300,
                         command=lambda v, ch=i: self._pwm_changed(ch))
            s.grid(row=1 + i, column=2, sticky="w")
            self.pwm_scale.append(s)
        ttk.Button(f, text=self.t["color"], command=self.pick_color).grid(row=7, column=0, sticky="w", pady=12)
        ttk.Button(f, text=self.t["beep"], command=lambda: self.cmd("BUZ 200")).grid(row=8, column=0, sticky="w")
        self._pwm_job = None

    def _tab_params(self, nb):
        f = ttk.Frame(nb, padding=4)
        nb.add(f, text=self.t["t_par"])
        cols = ("val", "min", "max", "def", "unit")
        self.ptv = ttk.Treeview(f, columns=cols, height=24)
        self.ptv.heading("#0", text="Parameter")
        for c, w in zip(cols, (90, 80, 80, 80, 50)):
            self.ptv.heading(c, text=c)
            self.ptv.column(c, width=w, anchor="e")
        self.ptv.column("#0", width=160)
        self.ptv.pack(side="left", fill="y")
        self.ptv.bind("<<TreeviewSelect>>", self._param_selected)
        r = ttk.Frame(f, padding=10)
        r.pack(side="left", fill="both", expand=True)
        self.pname = ttk.Label(r, text="–", font=("TkDefaultFont", 11, "bold"))
        self.pname.pack(anchor="w")
        self.pdesc = ttk.Label(r, text="", wraplength=420, justify="left")
        self.pdesc.pack(anchor="w", pady=4)
        self.pval = ttk.Entry(r, width=14)
        self.pval.pack(anchor="w", pady=4)
        for txt, fn in ((self.t["set"], self.set_param), (self.t["read"], lambda: self.cmd("PARS")),
                        (self.t["save"], lambda: self.cmd("SAVE")), (self.t["defaults"], lambda: self.cmd("DEFAULTS"))):
            ttk.Button(r, text=txt, command=fn, width=22).pack(anchor="w", pady=2)
        with open(os.path.join(HERE, "params_help.json"), encoding="utf-8") as fh:
            self.param_help = json.load(fh)

    def _tab_bus(self, nb):
        f = ttk.Frame(nb, padding=4)
        nb.add(f, text=self.t["t_bus"])
        bar = ttk.Frame(f)
        bar.pack(fill="x")
        self.mon485 = tk.IntVar()
        self.monspi = tk.IntVar()
        ttk.Checkbutton(bar, text=self.t["mon485"], variable=self.mon485,
                        command=lambda: self.cmd(f"MON 485 {self.mon485.get()}")).pack(side="left")
        ttk.Checkbutton(bar, text=self.t["monspi"], variable=self.monspi,
                        command=lambda: self.cmd(f"MON SPI {self.monspi.get()}")).pack(side="left", padx=10)
        ttk.Button(bar, text=self.t["clear"], command=lambda: self.log.delete("1.0", "end")).pack(side="left")
        self.entry = ttk.Entry(bar, width=40)
        self.entry.pack(side="left", padx=10)
        self.entry.bind("<Return>", lambda e: self._send_entry())
        ttk.Button(bar, text=self.t["send"], command=self._send_entry).pack(side="left")
        self.log = tk.Text(f, font=("DejaVu Sans Mono", 9), height=30)
        self.log.pack(fill="both", expand=True, pady=4)
        for tag, col in (("EVT", "#1b5e20"), ("BUS", "#0d47a1"), ("ERR", "#b71c1c"), ("OK", "#555555")):
            self.log.tag_configure(tag, foreground=col)

    def _tab_faults(self, nb):
        f = ttk.Frame(nb, padding=10)
        nb.add(f, text=self.t["t_flt"])
        ttk.Label(f, text=self.t["fault_hint"]).pack(anchor="w", pady=(0, 8))
        self.fault_var = {}
        for name in FAULTS:
            v = tk.IntVar()
            ttk.Checkbutton(f, text=name, variable=v,
                            command=lambda n=name, var=v: self.cmd(f"FAULT {n} {var.get()}")).pack(anchor="w")
            self.fault_var[name] = v
        ttk.Label(f, text=self.t["errors"]).pack(anchor="w", pady=(14, 2))
        self.err_list = tk.Listbox(f, height=10, width=60)
        self.err_list.pack(anchor="w")

    def _tab_lab(self, nb):
        f = ttk.Frame(nb, padding=6)
        nb.add(f, text=self.t["t_lab"])
        self.labs = []
        for p in sorted(glob.glob(os.path.join(HERE, "labs", "*.json"))):
            with open(p, encoding="utf-8") as fh:
                self.labs.append(json.load(fh))
        bar = ttk.Frame(f)
        bar.pack(fill="x")
        ttk.Label(bar, text=self.t["lab"]).pack(side="left")
        self.lab_cb = ttk.Combobox(bar, width=70, state="readonly",
                                   values=[lb["title"][self.lang] for lb in self.labs])
        self.lab_cb.pack(side="left", padx=4)
        ttk.Button(bar, text=self.t["start"], command=self.lab_start).pack(side="left")
        ttk.Button(bar, text=self.t["prev"], command=lambda: self.lab_goto(-1)).pack(side="left", padx=(12, 2))
        ttk.Button(bar, text=self.t["next"], command=lambda: self.lab_goto(1)).pack(side="left")
        ttk.Button(bar, text=self.t["report"], command=self.lab_report).pack(side="left", padx=12)
        self.lab_text = tk.Text(f, height=18, wrap="word", font=("DejaVu Sans", 11))
        self.lab_text.pack(fill="both", expand=True, pady=6)
        ttk.Label(f, text=self.t["notes"]).pack(anchor="w")
        self.lab_notes = tk.Text(f, height=6, wrap="word")
        self.lab_notes.pack(fill="x")

    # ------------------------------------------------------------ actions
    def cmd(self, c):
        if not self.link.connected:
            self.status.configure(text=self.t["notconn"])
            return
        self.link.send(c)
        self._log(">> " + c, "OK")

    def toggle_connect(self):
        if self.link.connected:
            self.link.close()
            self.btn_conn.configure(text=self.t["connect"])
            self.status.configure(text=self.t["notconn"])
            return
        try:
            self.link.open(self.port.get())
        except Exception as e:
            messagebox.showerror("Port", str(e))
            return
        self.btn_conn.configure(text=self.t["disconnect"])
        for c in ("INFO", "STREAM " + self.period.get(), "PARS", "FAULTS"):
            self.cmd(c)

    def toggle_relay(self, k):
        on = self.last.get("K", "00000")
        cur = len(on) > k and on[k] == "1"
        self.cmd(f"OUT K{k + 1} {0 if cur else 1}")

    def _pwm_changed(self, ch):
        if self._pwm_job:
            self.after_cancel(self._pwm_job)
        self._pwm_job = self.after(250, lambda: self.cmd(f"PWM {ch + 1} {self.pwm_scale[ch].get()}"))

    def pick_color(self):
        c = colorchooser.askcolor()[1]
        if c:
            self.cmd("RGB " + c[1:].upper())

    def _param_selected(self, _e=None):
        sel = self.ptv.selection()
        if not sel:
            return
        name = sel[0]
        self.pname.configure(text=name)
        self.pdesc.configure(text=self.param_help.get(name, {}).get(self.lang, ""))
        self.pval.delete(0, "end")
        self.pval.insert(0, self.params.get(name, {}).get("val", ""))

    def set_param(self):
        sel = self.ptv.selection()
        if sel:
            self.cmd(f"SET {sel[0]} {self.pval.get().strip()}")

    def _send_entry(self):
        c = self.entry.get().strip()
        if c:
            self.cmd(c)
            self.entry.delete(0, "end")

    def toggle_csv(self):
        if self.csv_file:
            self.csv_file.close()
            self.csv_file = self.csv_writer = None
            self.btn_rec.configure(text=self.t["rec"])
            return
        name = filedialog.asksaveasfilename(defaultextension=".csv",
                                            initialfile=dt.datetime.now().strftime("stand_%Y%m%d_%H%M%S.csv"))
        if name:
            self._csv_open(name)

    def _csv_open(self, name):
        self.csv_file = open(name, "w", newline="", encoding="utf-8")
        self.csv_writer = None
        self.btn_rec.configure(text=self.t["recstop"])

    # ------------------------------------------------------------ lab mode
    def lab_start(self):
        i = self.lab_cb.current()
        if i < 0:
            return
        self.lab = self.labs[i]
        self.lab_step = 0
        if not self.csv_file:
            os.makedirs(os.path.join(HERE, "reports"), exist_ok=True)
            self._csv_open(os.path.join(HERE, "reports", dt.datetime.now().strftime(
                f"{self.lab['id']}_%Y%m%d_%H%M%S.csv")))
        self.lab_show()

    def lab_goto(self, d):
        if not self.lab:
            return
        self.lab_step = max(0, min(len(self.lab["steps"]) - 1, self.lab_step + d))
        self.lab_show()

    def lab_show(self):
        lb, L = self.lab, self.lang
        st = lb["steps"][self.lab_step]
        self.lab_text.delete("1.0", "end")
        self.lab_text.insert("end", lb["title"][L] + "\n\n", ())
        self.lab_text.insert("end", lb["aim"][L] + "\n\n")
        self.lab_text.insert("end", f"{self.lab_step + 1}/{len(lb['steps'])}. {st['text'][L]}\n")
        for c in st.get("cmd", []):
            self.cmd(c)
            self.lab_text.insert("end", f"\n  → {c}")
        self._log(f"## LAB {lb['id']} step {self.lab_step + 1}", "EVT")

    def lab_report(self):
        if not self.lab:
            return
        os.makedirs(os.path.join(HERE, "reports"), exist_ok=True)
        name = os.path.join(HERE, "reports", dt.datetime.now().strftime(f"{self.lab['id']}_%Y%m%d_%H%M%S.txt"))
        with open(name, "w", encoding="utf-8") as fh:
            fh.write(self.lab["title"][self.lang] + "\n" + dt.datetime.now().isoformat(" ", "seconds") + "\n\n")
            fh.write(self.lab_notes.get("1.0", "end"))
            fh.write("\n--- last values ---\n")
            for k, v in self.last.items():
                fh.write(f"{k} = {v}\n")
        messagebox.showinfo("Report", name)

    # ------------------------------------------------------------ incoming data
    def _log(self, s, tag=None):
        self.log.insert("end", time.strftime("%H:%M:%S ") + s + "\n", tag)
        if int(self.log.index("end-1c").split(".")[0]) > 3000:
            self.log.delete("1.0", "500.0")
        self.log.see("end")

    def _poll(self):
        try:
            while True:
                tag, payload, raw = self.link.lines.get_nowait()
                self._handle(tag, payload, raw)
        except queue.Empty:
            pass
        self.after(50, self._poll)

    def _handle(self, tag, payload, raw):
        if tag == "TEL":
            self._telemetry(parse_kv(payload))
        elif tag == "PAR":
            name, _, rest = payload.partition("=")
            val, _, more = rest.partition(";")
            d = {"val": val, **{k: str(v) for k, v in parse_kv(more).items()}}
            self.params[name] = d
            vals = (val, d.get("min", ""), d.get("max", ""), d.get("def", ""), d.get("unit", ""))
            if self.ptv.exists(name):
                self.ptv.item(name, values=vals)
            else:
                self.ptv.insert("", "end", iid=name, text=name, values=vals)
            self._log(raw, "OK")
        elif tag == "INFO":
            self.status.configure(text=payload.replace(";", "  "), foreground="#1b5e20")
            self._log(raw, "OK")
        elif tag == "OK" and payload.startswith("FAULTS"):
            for item in payload.split()[1:]:
                n, _, v = item.partition("=")
                if n in self.fault_var:
                    self.fault_var[n].set(int(v))
            self._log(raw, "OK")
        elif tag == "LINK":
            self._log("LINK LOST: " + payload, "ERR")
            self.link.close()
            self.btn_conn.configure(text=self.t["connect"])
        else:
            if tag == "EVT" and payload.startswith("FAULT "):
                parts = payload.split()
                if len(parts) == 3 and parts[1] in self.fault_var:
                    self.fault_var[parts[1]].set(1 if parts[2] == "ON" else 0)
            self._log(raw, tag if tag in ("EVT", "BUS", "ERR", "OK") else None)

    def _telemetry(self, d):
        self.last = d
        for key, *_ in QUANT:
            v = d.get(key)
            if isinstance(v, float):
                v = f"{v:.0f}" if key in INT_KEYS else (f"{v:.2f}" if key.startswith("T") else f"{v:.1f}")
            self.tv.item(key, values=("–" if v is None else v, self.tv.item(key, "values")[1]))
        mode = d.get("mode")
        if mode in ("AUTO", "MANUAL"):
            self.mode.set(mode)
        k = d.get("K") or "00000"
        for i, b in enumerate(self.relay_btn):
            on = len(k) > i and k[i] == "1"
            b.configure(text=self.t["relays"][i] + (": ON" if on else ": OFF"), bg="#ffcc80" if on else "#e0e0e0")
        err = int(d.get("ERR") or "0", 16)
        errs = decode_errors(err)
        self.err.configure(text=f'{self.t["errors"]} {err:08X} – ' + ("; ".join(errs) if errs else self.t["noerr"]),
                           bg="#fdecea" if err & 0x08010000 else "#eef3f9")
        self.err_list.delete(0, "end")
        for e in errs:
            self.err_list.insert("end", e)
        # history
        now = time.time()
        h = self.hist
        h["t"].append(now)
        for key in ("T1", "T2", "TA", "RH", "W1", "W2", "P1"):
            h[key].append(d.get(key))
        h["K1"].append(1 if k[0] == "1" else 0)
        while h["t"] and now - h["t"][0] > HISTORY_S:
            for key in h:
                h[key].pop(0)
        self._redraw()
        if self.csv_file:
            if self.csv_writer is None:
                self.csv_writer = csv.DictWriter(self.csv_file, fieldnames=["pc_time"] + list(d.keys()),
                                                 extrasaction="ignore")
                self.csv_writer.writeheader()
            self.csv_writer.writerow({"pc_time": dt.datetime.now().isoformat(" ", "seconds"), **d})
            self.csv_file.flush()

    def _redraw(self):
        h = self.hist
        if len(h["t"]) < 2:
            return
        x = [t - h["t"][-1] for t in h["t"]]
        uk = self.lang == "uk"
        for ax in (self.ax1, self.ax2, self.ax3):
            ax.clear()
            ax.grid(alpha=0.3)
        self.ax1.plot(x, h["T1"], label="T1")
        self.ax1.plot(x, h["T2"], label="T2")
        self.ax1.plot(x, h["TA"], label="TA", alpha=0.6)
        p = self.params.get("HEAT_TSET", {}).get("val")
        if p:
            self.ax1.axhline(float(p), color="k", lw=0.8, ls="--", label="HEAT_TSET")
        self.ax1.set_ylabel("°C")
        self.ax1.legend(loc="upper left", fontsize=8, ncol=4)
        self.ax2.plot(x, h["RH"], label="RH")
        self.ax2.plot(x, h["W1"], label="W1")
        self.ax2.plot(x, h["W2"], label="W2")
        self.ax2.set_ylabel("%")
        self.ax2.legend(loc="upper left", fontsize=8, ncol=3)
        self.ax3.step(x, [v * 100 for v in h["K1"]], where="post", label="K1 (heater)" if not uk else "K1 нагрівач")
        self.ax3.plot(x, h["P1"], label="PW1 (fan)" if not uk else "PW1 вентилятор")
        self.ax3.set_ylabel("%")
        self.ax3.set_ylim(-5, 105)
        self.ax3.set_xlabel("s" if not uk else "с")
        self.ax3.legend(loc="upper left", fontsize=8, ncol=2)
        self.canvas.draw_idle()


def main():
    ap = argparse.ArgumentParser(description="D2.GreenHouse stand control program")
    ap.add_argument("--lang", choices=("uk", "en"), default="uk")
    ap.add_argument("--port", help="connect to this port at start")
    a = ap.parse_args()
    app = App(a.lang)
    if a.port:
        app.port.set(a.port)
        app.after(300, app.toggle_connect)
    app.mainloop()


if __name__ == "__main__":
    main()
