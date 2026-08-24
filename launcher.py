import tkinter as tk
from tkinter import ttk
import os

RESOLUTIONS = [
    "1280x720",
    "1600x900",
    "1920x1080",
]

class LauncherApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Mahjong Launcher")
        self.resizable(False, False)
         

        pad = {"padx": 10, "pady": 5}

        tk.Label(self, text="Resolution:").grid(row=0, column=0, sticky="w", **pad)
        self.resolution_combo = ttk.Combobox(
            self, values=RESOLUTIONS, state="readonly", width=20
        )
        self.resolution_combo.current(0)
        self.resolution_combo.grid(row=0, column=1, sticky="w", **pad)

        self.aa_var = tk.BooleanVar(value=True)
        tk.Checkbutton(self, text="Antialiasing", variable=self.aa_var).grid(
            row=1, column=0, columnspan=2, sticky="w", **pad
        )

        self.host_mode_var = tk.BooleanVar(value=True)
        tk.Checkbutton(
            self,
            text="Host Mode",
            variable=self.host_mode_var,
            command=self.update_host_client_ui,
        ).grid(row=2, column=0, columnspan=2, sticky="w", **pad)

        self.dynamic_frame = tk.Frame(self)
        self.dynamic_frame.grid(row=3, column=0, columnspan=2, sticky="w", **pad)

        self.clients_label = tk.Label(self.dynamic_frame, text="Number of Clients:")
        self.clients_combo = ttk.Combobox(
            self.dynamic_frame, values=["0", "1", "2", "3"], state="readonly", width=5
        )
        self.clients_combo.current(0)

        self.host_address_label = tk.Label(self.dynamic_frame, text="Host Address:")
        self.host_address_var = tk.StringVar(value="127.0.0.1")
        self.host_address_entry = tk.Entry(
            self.dynamic_frame, textvariable=self.host_address_var, width=20
        )

        self.update_host_client_ui()

        tk.Button(self, text="Launch", command=self.launch_game, width=15).grid(
            row=4, column=0, columnspan=2, pady=15
        )

    def update_host_client_ui(self):
        """Show num-clients dropdown in host mode, host-address entry in client mode."""
        for widget in self.dynamic_frame.winfo_children():
            widget.grid_forget()

        if self.host_mode_var.get():
            self.clients_label.grid(row=0, column=0, sticky="w")
            self.clients_combo.grid(row=0, column=1, sticky="w", padx=5)
        else:
            self.host_address_label.grid(row=0, column=0, sticky="w")
            self.host_address_entry.grid(row=0, column=1, sticky="w", padx=5)

    def build_args(self):
        args = []

        mode = self.resolution_combo.current()
        args += ["--screen-mode", str(mode)]

        if not self.aa_var.get():
            args.append("--no-aa")

        if self.host_mode_var.get():
            args.append("--host")
            args += ["--num-clients", self.clients_combo.get()]
        else:
            host_address = self.host_address_var.get().strip()
            args += ["--client", host_address]

        return args

    def launch_game(self):
        args = self.build_args()
        launch = ["mahjong.exe"] + args
        os.execvp("mahjong.exe", launch)


if __name__ == "__main__":
    app = LauncherApp()
    app.geometry("280x180")
    app.mainloop()
