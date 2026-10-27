#!/usr/bin/env python3
# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Rebuild the five README SVGs: python3 assets/readme/generate.py.

Standard-library only. SVGs are connection diagrams, not board pin-placement
maps. Inspect actual component markings. Colors supplement explicit net labels.
"""
from pathlib import Path
from html import escape

OUT = Path(__file__).resolve().parent
INK, MUTED, EDGE = '#142b3b', '#425c6d', '#c5d4dd'
BLUE, RED, GROUND, PURPLE = '#0969aa', '#ba2e34', '#334155', '#7652ac'
GREEN, YELLOW, ORANGE, OFF = '#18a84d', '#e8b300', '#ef741c', '#41554d'

class Svg:
    def __init__(self, name, height, title, description):
        self.name = name
        self.parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="720" height="{height}" viewBox="0 0 720 {height}" role="img" aria-labelledby="title desc">',
            f'<title id="title">{escape(title)}</title><desc id="desc">{escape(description)}</desc>',
            '<style>text{font-family:Arial,Helvetica,sans-serif}path,line,rect,circle{stroke-linecap:round;stroke-linejoin:round}</style>',
            f'<rect width="720" height="{height}" fill="#ffffff"/>']
        self.text(28,44,title,30,bold=True)
    def text(self,x,y,s,size=24,color=INK,bold=False,anchor='start'):
        self.parts.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" font-weight="{700 if bold else 400}" text-anchor="{anchor}">{escape(s)}</text>')
    def rect(self,x,y,w,h,fill='#f3f7fa',stroke=EDGE,r=14):
        self.parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
    def line(self,points,color=GROUND,width=4):
        d='M '+' L '.join(f'{x},{y}' for x,y in points)
        self.parts.append(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}"/>')
    def dot(self,x,y,color=GROUND,r=5):
        self.parts.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>')
    def arrow(self,x,y,direction='right',color=BLUE):
        pts={'right':[(x-12,y-8),(x,y),(x-12,y+8)],'down':[(x-8,y-12),(x,y),(x+8,y-12)]}[direction]
        self.line(pts,color)
    def ground(self,x,y):
        self.line([(x,y),(x,y+12)])
        for dy,half in [(12,16),(20,10),(28,4)]: self.line([(x-half,y+dy),(x+half,y+dy)])
    def resistor(self,x,y,vertical=False):
        if vertical:
            self.line([(x,y),(x,y+12)]);self.rect(x-10,y+12,20,42,'#f8e8b7',GROUND,2);self.line([(x,y+54),(x,y+66)])
        else:
            self.line([(x,y),(x+15,y)]);self.rect(x+15,y-10,52,20,'#f8e8b7',GROUND,2);self.line([(x+67,y),(x+82,y)])
    def strip(self,x,y,colors,step=67,size=45):
        self.rect(x-12,y-12,step*5+size+24,size+24,'#244238', '#244238',8)
        for i,c in enumerate(colors):
            self.rect(x+i*step,y,size,size,c,'#9eb7aa',6)
    def cap(self,x,y,bulk=False):
        self.line([(x,y),(x,y+24)],RED)
        self.line([(x-20,y+24),(x+20,y+24)],GROUND)
        self.line([(x-20,y+36),(x+20,y+36)],GROUND,7 if bulk else 4)
        self.line([(x,y+36),(x,y+62)],GROUND)
        if bulk: self.text(x-42,y+23,'+',26,RED,True);self.text(x+30,y+47,'−',26,GROUND,True)
    def save(self):
        (OUT/self.name).write_text('\n'.join(self.parts+['</svg>'])+'\n',encoding='utf-8')

def overview():
    s=Svg('hardware-overview.svg',905,'Gather these parts','Parts for direct GPIO27 data through 330 ohms. No level shifter or CAN connection in the standard demo.')
    s.text(28,82,'Direct-drive demo • CAN stays disconnected',24,MUTED)
    items=[('ESP32 DevKit V1','USB-powered ESP32'),('6 × WS2812B LEDs','5V • GRB • 800kHz'),
        ('330Ω resistor','In series with DIN'),('1000µF capacitor','10V • polarized'),
        ('Regulated 5V supply','At least 1A for LEDs'),('USB data cable','ESP32 power + upload'),
        ('Secure wires','Short data + common GND'),('Multimeter','Check voltage / polarity')]
    for i,(title,note) in enumerate(items):
        x=28+(i%2)*348;y=108+(i//2)*180
        s.rect(x,y,320,164)
        if i==0:
            s.rect(x+110,y+14,100,57,'#244238',GROUND,4);s.rect(x+135,y+21,50,30,'#b7c4cc',GROUND,2)
            for pin in range(6):
                s.line([(x+102,y+20+pin*8),(x+110,y+20+pin*8)]);s.line([(x+210,y+20+pin*8),(x+218,y+20+pin*8)])
        elif i==1:s.strip(x+38,y+29,[GREEN,GREEN,YELLOW,YELLOW,ORANGE,RED],step=43,size=29)
        elif i==2:s.resistor(x+117,y+45)
        elif i==3:
            s.rect(x+130,y+15,55,50,'#263647',GROUND,6);s.rect(x+165,y+18,15,44,'#e6edf3','#e6edf3',2)
            s.text(x+168,y+47,'−',17);s.line([(x+140,y+65),(x+140,y+83)]);s.line([(x+173,y+65),(x+173,y+76)])
        elif i==4:
            s.rect(x+112,y+16,98,58,GROUND,GROUND,8);s.text(x+161,y+53,'5V',27,'#fff',True,'middle');s.line([(x+210,y+45),(x+246,y+45)],RED)
        elif i==5:
            s.line([(x+106,y+38),(x+140,y+38),(x+140,y+61),(x+193,y+61),(x+193,y+38),(x+214,y+38)],PURPLE,6)
            s.rect(x+82,y+24,30,28,'#b7c4cc',GROUND,3);s.rect(x+211,y+28,24,20,'#b7c4cc',GROUND,3)
        elif i==6:
            for dy,color in [(25,BLUE),(43,GROUND),(61,RED)]:s.line([(x+100,y+dy),(x+220,y+dy)],color,5)
        else:
            s.rect(x+123,y+12,75,66,'#dfb643',GROUND,7);s.rect(x+132,y+19,57,24,'#e2efe6',GROUND,2)
            s.dot(x+160,y+60,GROUND,9)
        s.text(x+160,y+111,title,24,bold=True,anchor='middle');s.text(x+160,y+143,note,22,MUTED,anchor='middle')
    s.text(28,864,'Illustrations identify parts; check the actual markings.',23,MUTED)
    s.text(28,894,'Direct 3.3V data requires a compatible 5V LED strip.',23,RED)
    s.save()

def wiring():
    s=Svg('main-wiring.svg',1185,'Direct GPIO27 LED wiring','ESP32 GPIO27 at 3.3V through 330 ohms into LED DIN. USB powers ESP32, external regulated 5V powers the six LEDs. Grounds join and the 1000uF 10V capacitor is across LED power. No CAN module or level shifter is connected.')
    s.text(28,84,'Assemble and check with BOTH supplies disconnected.',23,MUTED)
    s.text(28,116,'BLUE = 3.3V data   RED = +5V   BLACK = GND',23,MUTED)
    s.text(235,151,'USB power',25,PURPLE,True)
    s.line([(360,154),(360,175)],PURPLE);s.arrow(360,175,'down',PURPLE)
    s.rect(220,175,280,145)
    s.text(243,215,'ESP32 DevKit V1',25,bold=True)
    s.text(243,255,'GPIO27 · 3.3V data',24,BLUE)
    s.text(243,292,'GND',24,GROUND)
    s.line([(220,280),(40,280),(40,900),(680,900)],GROUND)
    s.line([(360,320),(360,345)],BLUE);s.resistor(360,345,True)
    s.text(400,383,'330Ω',27,bold=True)
    s.line([(360,411),(360,426),(80,426),(80,495),(148,495)],BLUE);s.arrow(148,495)
    s.text(64,398,'Keep the data lead short.',22,BLUE)
    s.strip(160,470,[OFF]*6,step=67,size=45)
    s.text(144,455,'DIN',25,BLUE,True)
    s.line([(552,495),(630,495)],BLUE);s.dot(630,495,BLUE,4)
    s.text(495,455,'DOUT unused',24,MUTED)
    s.text(241,563,'Six LEDs →',24,bold=True)
    s.line([(200,527),(200,620),(40,620)],GROUND);s.dot(40,620)
    s.text(118,575,'GND',24,GROUND)
    s.line([(480,527),(480,600),(650,600),(650,760)],RED)
    s.text(492,575,'+5V',24,RED,True)
    s.line([(650,660),(360,660),(360,710)],RED);s.dot(650,660,RED)
    s.cap(360,710,True);s.line([(360,772),(360,900)],GROUND);s.dot(360,900)
    s.text(69,716,'1000µF / 10V',25,bold=True)
    s.text(69,755,'+ lead to LED +5V',23,RED)
    s.text(69,793,'− striped side to GND',23)
    s.rect(440,760,240,100,'#fff2f0')
    s.text(459,792,'Regulated 5V',25,RED,True)
    s.text(459,824,'At least 1A',24)
    s.text(628,751,'+',27,RED,True)
    s.line([(500,860),(500,900)],GROUND);s.dot(500,900)
    s.text(517,888,'Supply GND',22)
    s.text(28,935,'Common GND: ESP32 + LEDs + supply negative',23,bold=True)
    s.rect(28,963,664,92,'#fff1f0','#e8b1ac')
    s.text(44,997,'DO NOT connect external +5V',27,RED,True)
    s.text(44,1031,'to USB-powered ESP32 5V / VIN / 3V3 pins.',24,RED)
    s.text(28,1094,'CAN module disconnected; GPIO16/17 reserved.',24,bold=True)
    s.text(28,1133,'Direct 3.3V data is strip-dependent.',25,RED,True)
    s.text(28,1170,'A compatible strip must be confirmed by physical testing.',23,MUTED)
    s.save()

def direction():
    s=Svg('led-direction.svg',390,'Find the strip INPUT','Direct 3.3V ESP32 data through 330 ohms connects to DIN. Follow the printed arrow toward DOUT, which is unused.')
    s.text(28,86,'GPIO27 → 330Ω → DIN (3.3V data)',25,BLUE)
    s.line([(44,110),(44,192),(108,192)],BLUE);s.arrow(108,192)
    s.strip(120,170,[OFF]*6,step=82,size=46)
    s.text(112,260,'DIN / IN',26,BLUE,True);s.text(512,260,'DOUT / OUT',26,bold=True)
    s.line([(252,270),(490,270)],BLUE);s.arrow(490,270)
    s.text(282,307,'Printed arrow',23,MUTED)
    s.text(28,355,'Connect DIN. Leave DOUT unused.',25,bold=True)
    s.save()

def safety():
    s=Svg('power-safety.svg',825,'Two supplies. One common ground.','USB powers only the ESP32. Regulated external 5V powers only the LEDs. Their grounds join. Capacitor positive connects to 5V, striped negative to ground. Check polarity before power.')
    s.text(28,84,'Check polarity with all power disconnected.',25,RED,True)
    s.rect(30,125,285,92,'#f3effb');s.text(52,164,'USB power',27,PURPLE,True);s.text(52,197,'ESP32 USB socket only',22)
    s.rect(405,125,285,92,'#fff2f0');s.text(425,164,'Regulated 5V ≥1A',25,RED,True);s.text(425,197,'To LED strip only',23)
    s.line([(172,217),(172,288)],PURPLE);s.arrow(172,288,'down',PURPLE)
    s.line([(548,217),(548,288)],RED);s.arrow(548,288,'down',RED)
    s.rect(65,288,250,94);s.text(90,327,'ESP32',28,bold=True);s.text(90,360,'USB-powered',23)
    s.rect(405,288,285,94);s.text(425,327,'6 × WS2812B LEDs',24,bold=True);s.text(425,360,'External 5V-powered',22)
    s.line([(172,382),(172,434),(548,434),(548,382)],GROUND,5);s.dot(360,434);s.ground(360,434)
    s.text(40,488,'Join GND: ESP32 + LEDs + supply negative',23,bold=True)
    s.rect(30,515,660,122)
    s.rect(66,537,66,77,GROUND,GROUND,7);s.rect(108,541,18,69,'#e3eaf0','#e3eaf0',2);s.text(108,581,'−',20)
    s.text(155,550,'1000µF / 10V electrolytic',25,bold=True)
    s.text(155,587,'+ lead → LED +5V',24,RED)
    s.text(155,620,'− striped side → GND',24)
    s.text(30,680,'ON: LED supply first, then ESP32 USB.',24,bold=True)
    s.text(30,720,'OFF: unplug ESP32 USB first, then LED supply.',24,bold=True)
    s.text(30,764,'Never join the two positive power rails.',26,RED,True)
    s.text(30,799,'No live rewiring. Keep common ground connected.',23,MUTED)
    s.save()

def expected():
    s=Svg('expected-behavior.svg',960,'What a working demo looks like','Illustrative LED states: startup sweep and pulse, progressive classic colors, flashing red shift alert, then deliberate stale-data blanking and recovery. Not measured engine RPM.')
    s.text(28,83,'Illustrative snapshots • simulated RPM only',24,MUTED)
    stages=[('1  STARTUP','Short sweep forward/back, then a pulse.',[OFF,GREEN,OFF,OFF,OFF,OFF]),('2  PROGRESSIVE FILL','More pixels light as the demo RPM rises.',[GREEN,GREEN,YELLOW,OFF,OFF,OFF]),('3  SHIFT ALERT','All six flash red at 5800 simulated RPM.',[RED]*6),('4  TEST PAUSE + RECOVERY','A planned dark interval, then lights return.',[OFF]*6)]
    for i,(title,note,colors) in enumerate(stages):
        y=113+i*196;s.rect(28,y,664,177);s.text(49,y+36,title,26,bold=True);s.text(49,y+71,note,23,MUTED)
        s.strip(64,y+104,colors,step=70,size=40)
        s.text(514,y+134,'ON / OFF' if i==2 else ('→ ←' if i==0 else ''),23,RED if i==2 else BLUE,True)
    s.text(28,916,'Dark squares = off. Colors are illustrative.',23,MUTED)
    s.text(28,947,'Default brightness: 40/255. No settings to change.',23,MUTED)
    s.save()

if __name__=='__main__':
    for make in (overview,wiring,direction,safety,expected): make()
    print('Wrote five README SVGs in assets/readme/')
