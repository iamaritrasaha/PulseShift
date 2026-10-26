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
    s=Svg('hardware-overview.svg',1080,'Gather these parts','Nine labeled component illustrations for the standard six-pixel demonstration. CAN and OLED are not required.')
    s.text(28,82,'Standard demo • no CAN module needed',24,MUTED)
    items=[('ESP32 DevKit V1','Original ESP32 board'),('6 × WS2812B LEDs','5V • GRB • 800kHz'),('SN74AHCT125N','DIP-14 level shifter'),('330Ω resistor','In series with DIN'),('10kΩ resistor','Input pull-down'),('1000µF capacitor','10V • polarized'),('100nF capacitor','0.1µF • ceramic'),('Regulated 5V supply','At least 1A'),('USB data cable','ESP32 power + upload')]
    for i,(title,note) in enumerate(items):
        col=i%2; row=i//2;x=28+col*348;y=108+row*180
        s.rect(x,y,320,164)
        if i==0:
            s.rect(x+110,y+14,100,57,'#244238',GROUND,4);s.rect(x+135,y+21,50,30,'#b7c4cc',GROUND,2)
            for p in range(6):
                s.line([(x+102,y+20+p*8),(x+110,y+20+p*8)]);s.line([(x+210,y+20+p*8),(x+218,y+20+p*8)])
        elif i==1:s.strip(x+38,y+29,[GREEN,GREEN,YELLOW,YELLOW,ORANGE,RED],step=43,size=29)
        elif i==2:
            s.rect(x+105,y+24,112,35,GROUND,GROUND,3)
            for p in range(7):
                s.line([(x+114+p*15,y+17),(x+114+p*15,y+24)]);s.line([(x+114+p*15,y+59),(x+114+p*15,y+68)])
            s.dot(x+114,y+34,'#fff',3)
        elif i in (3,4):s.resistor(x+117,y+45)
        elif i==5:
            s.rect(x+130,y+15,55,50,'#263647',GROUND,6);s.rect(x+165,y+18,15,44,'#e6edf3','#e6edf3',2)
            s.text(x+168,y+47,'−',17);s.line([(x+140,y+65),(x+140,y+83)]);s.line([(x+173,y+65),(x+173,y+76)])
        elif i==6:
            s.dot(x+160,y+37,'#d59632',22);s.line([(x+150,y+57),(x+150,y+78)]);s.line([(x+170,y+57),(x+170,y+78)])
        elif i==7:
            s.rect(x+112,y+16,98,58,GROUND,GROUND,8);s.text(x+161,y+53,'5V',27,'#fff',True,'middle');s.line([(x+210,y+45),(x+246,y+45)],RED)
        else:
            s.line([(x+106,y+38),(x+140,y+38),(x+140,y+61),(x+193,y+61),(x+193,y+38),(x+214,y+38)],PURPLE,6)
            s.rect(x+82,y+24,30,28,'#b7c4cc',GROUND,3);s.rect(x+211,y+28,24,20,'#b7c4cc',GROUND,3)
        s.text(x+160,y+111,title,24,bold=True,anchor='middle');s.text(x+160,y+143,note,22,MUTED,anchor='middle')
    s.text(380,880,'Also have ready:',24,bold=True)
    for j,line in enumerate(['Secure wires / connectors','Breadboard or soldered board','Multimeter for checks']):s.text(380,918+j*34,line,21,MUTED)
    s.text(28,1048,'Illustrations identify parts; check the real markings.',23,MUTED)
    s.save()

def wiring():
    s=Svg('main-wiring.svg',1560,'Wire the standalone demo','GPIO27 to AHCT125 pin 2; 10k pull-down to ground; pin 3 through 330 ohms to DIN. USB powers ESP32, external 5V powers strip and shifter. All GND labels connect together. Both capacitors connect across the stated supplies.')
    s.text(28,83,'Connection map, not physical board pin positions',23,MUTED)
    s.text(28,120,'BLUE = data   RED = external 5V   GND = common',22,MUTED)
    s.rect(40,172,270,135);s.text(62,207,'ESP32 DevKit V1',25,bold=True);s.text(62,246,'GPIO27',25,BLUE,True);s.text(62,282,'GND → common ground',21)
    s.text(45,152,'USB powers this board',23,PURPLE)
    s.rect(440,172,240,200);s.text(458,207,'SN74AHCT125N',23,bold=True);s.text(458,250,'2 · 1A input',24,BLUE);s.text(458,299,'3 · 1Y output',24,BLUE);s.text(458,344,'Pins below ↓',23,MUTED)
    s.line([(310,240),(440,240)],BLUE);s.arrow(440,240)
    s.dot(365,240,BLUE);s.line([(365,240),(365,275)],BLUE);s.resistor(365,275,True);s.ground(365,341)
    s.text(249,321,'10kΩ',24);s.text(259,365,'GND',22)
    s.line([(560,372),(560,413),(88,413),(88,440)],BLUE)
    s.resistor(88,440,True);s.line([(88,506),(88,576),(138,576)],BLUE);s.arrow(138,576)
    s.text(124,468,'330Ω',26,bold=True);s.text(124,501,'Place close to the first LED',22,MUTED)
    s.strip(150,551,[OFF]*6,step=71,size=42)
    s.text(139,635,'DIN',24,BLUE,True);s.text(350,635,'6 LEDs →',24);s.text(566,635,'DOUT',24,bold=True)
    s.text(470,671,'Leave DOUT unused',22,MUTED)
    s.rect(28,710,664,106,'#fff3e8','#e8b885')
    s.text(46,744,'External regulated 5V, ≥1A',26,RED,True)
    s.text(46,777,'+5V → strip +5V and shifter pin 14',24)
    s.text(46,803,'Negative → every GND shown here',23)
    s.rect(28,839,325,320)
    s.text(46,876,'Shifter pin connections',22,bold=True)
    for j,(line,color) in enumerate([('14 · VCC → +5V',RED),('7 · GND → GND',GROUND),('1 · OE → GND',GROUND),('4, 10, 13 → +5V',RED),('5, 9, 12 → GND',GROUND),('6, 8, 11 → unused',MUTED)]):s.text(46,918+j*39,line,23,color)
    s.rect(375,839,317,150)
    s.text(390,872,'100nF at the shifter',23,bold=True)
    s.cap(565,900);s.text(395,914,'14 · +5V',22,RED);s.text(395,964,'7 · GND',22,GROUND)
    s.line([(500,900),(565,900)],RED);s.line([(500,962),(565,962)],GROUND)
    s.rect(375,1009,317,150)
    s.text(390,1043,'1000µF / 10V at LEDs',23,bold=True)
    s.cap(565,1070,True);s.text(392,1084,'Strip +5V',21,RED);s.text(392,1134,'Strip GND',21,GROUND)
    s.line([(500,1070),(565,1070)],RED);s.line([(500,1132),(565,1132)],GROUND)
    s.rect(28,1183,664,113)
    s.text(46,1218,'All GND labels mean the SAME connected wire:',23,bold=True)
    s.text(46,1253,'ESP32 + strip + shifter + supply negative',25)
    s.text(46,1280,'Also join the pull-down and capacitor grounds.',22,MUTED)
    s.rect(28,1320,664,102,'#fff1f0','#e8b1ac')
    s.text(46,1355,'DO NOT connect external +5V',27,RED,True)
    s.text(46,1391,'to the USB-powered ESP32 5V / VIN / 3V3 pins.',23,RED)
    s.text(28,1461,'CAN module: NOT connected in this demo.',25,bold=True)
    s.text(28,1500,'Leave GPIO16 and GPIO17 reserved and unconnected.',23,MUTED)
    s.text(28,1536,'Use the power-order picture before switching on.',23,MUTED)
    s.save()

def direction():
    s=Svg('led-direction.svg',390,'Find the strip INPUT','Buffered ESP32 data through the resistor connects to DIN. Follow the printed arrow toward DOUT, which is unused.')
    s.text(28,86,'Signal from GPIO27 via shifter + 330Ω',25,BLUE)
    s.line([(44,110),(44,192),(108,192)],BLUE);s.arrow(108,192)
    s.strip(120,170,[OFF]*6,step=82,size=46)
    s.text(112,260,'DIN / IN',26,BLUE,True);s.text(512,260,'DOUT / OUT',26,bold=True)
    s.line([(252,270),(490,270)],BLUE);s.arrow(490,270)
    s.text(282,307,'Printed arrow',23,MUTED)
    s.text(28,355,'Connect DIN. Leave DOUT unused.',25,bold=True)
    s.save()

def safety():
    s=Svg('power-safety.svg',825,'Two supplies. One common ground.','USB powers only the ESP32. Regulated external 5V powers the LEDs and shifter. Their grounds join. Capacitor positive connects to 5V, striped negative to ground. Check polarity before power.')
    s.text(28,84,'Check polarity with all power disconnected.',25,RED,True)
    s.rect(30,125,285,92,'#f3effb');s.text(52,164,'USB power',27,PURPLE,True);s.text(52,197,'ESP32 USB socket only',22)
    s.rect(405,125,285,92,'#fff2f0');s.text(425,164,'Regulated 5V ≥1A',25,RED,True);s.text(425,197,'To LEDs + shifter only',23)
    s.line([(172,217),(172,288)],PURPLE);s.arrow(172,288,'down',PURPLE)
    s.line([(548,217),(548,288)],RED);s.arrow(548,288,'down',RED)
    s.rect(65,288,250,94);s.text(90,327,'ESP32',28,bold=True);s.text(90,360,'USB-powered',23)
    s.rect(405,288,285,94);s.text(425,327,'LEDs + 74AHCT125',24,bold=True);s.text(425,360,'External 5V-powered',22)
    s.line([(172,382),(172,434),(548,434),(548,382)],GROUND,5);s.dot(360,434);s.ground(360,434)
    s.text(40,488,'Join GND: ESP32 + LEDs + shifter + supply negative',23,bold=True)
    s.rect(30,515,660,122)
    s.rect(66,537,66,77,GROUND,GROUND,7);s.rect(108,541,18,69,'#e3eaf0','#e3eaf0',2);s.text(108,581,'−',20)
    s.text(155,550,'1000µF / 10V electrolytic',25,bold=True)
    s.text(155,587,'+ lead → LED +5V',24,RED)
    s.text(155,620,'− striped side → GND',24)
    s.text(30,680,'ON: LED/shifter supply first, then ESP32 USB.',24,bold=True)
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
