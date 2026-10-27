# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Optional adapter for an already-running Wokwi browser, using only its UART UI.
No simulator state, firmware values or CAN events are injected. Requires the
documented custom-firmware loader and a WSL playwright-cli browser session.
"""
from collections import deque
from pathlib import Path
import json,subprocess,time
class BrowserSerial:
    def __init__(self,cli,session):
        self.cli=str(Path(cli).resolve());self.session=session
        self.previous="";self.pending="";self.lines=deque()
    def call(self,*args):
        result=subprocess.run([self.cli,f"-s={self.session}",*args],text=True,capture_output=True,timeout=20)
        if result.returncode:
            raise RuntimeError("Browser UART command failed: "+result.stdout+result.stderr)
        return result.stdout.strip()
    def readline(self):
        if not self.lines:
            current=json.loads(self.call("--raw","eval","document.querySelector('[role=tabpanel]').textContent"))
            if not isinstance(current,str):
                raise RuntimeError("Browser UART panel was not found.")
            if current.startswith(self.previous):
                added=current[len(self.previous):]
            else:
                anchor=self.previous[-1024:]
                at=current.find(anchor)
                if not anchor or at<0:
                    raise RuntimeError("Browser UART history lost; do not claim an uninterrupted capture.")
                added=current[at+len(anchor):]
            self.previous=current
            self.pending+=added
            split=self.pending.split("\n");self.pending=split.pop()
            self.lines.extend(line.rstrip("\r")+"\n" for line in split)
        if self.lines:return self.lines.popleft().encode()
        time.sleep(.1)
        return b""
    def write(self,data):
        command=data.decode().rstrip("\r\n")
        code="async page => { const input=page.locator('input:is([type=text],:not([type])):visible'); await input.fill("+json.dumps(command)+"); await input.press('Enter'); }"
        self.call("run-code",code)
        return len(data)
    def close(self):
        pass # Caller owns the browser; preserve it for rendered-output inspection.
