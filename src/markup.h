/* The whole window. GFOpenLayout lays this out; the program only fills
 * the fields and answers Open, Slice and a chosen layer.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GS_MARKUP_H
#define GS_MARKUP_H

static const char gs_markup[] =
    "<window>"
    "<row><h2>GFOpenSlicer</h2><spacer></spacer><span id=\"ver\">0.1.0-rc1</span></row>"
    "<row>"
    "<span>STL</span>"
    "<input id=\"path\" style=\"flex:1\">"
    "<button id=\"open\">Open</button>"
    "<button id=\"slice\">Slice</button>"
    "</row>"
    "<row>"
    "<span>Layer mm</span><spinner id=\"layer\" min=\"0.05\" max=\"0.80\" step=\"0.05\" value=\"0.20\">"
    "<span>Infill %</span><spinner id=\"infill\" min=\"0\" max=\"100\" step=\"5\" value=\"20\">"
    "<span>Line mm</span><spinner id=\"line\" min=\"0.20\" max=\"1.20\" step=\"0.05\" value=\"0.40\">"
    "<checkbox id=\"skirt\" checked>Skirt</checkbox>"
    "</row>"
    "<row>"
    "<span>Filament</span><spinner id=\"filament\" min=\"1.00\" max=\"3.00\" step=\"0.05\" value=\"1.75\">"
    "<span>Hotend</span><spinner id=\"hotend\" min=\"0\" max=\"300\" step=\"5\" value=\"200\">"
    "<span>Bed</span><spinner id=\"bed\" min=\"0\" max=\"120\" step=\"5\" value=\"60\">"
    "</row>"
    "<row id=\"body\">"
    "<list id=\"layers\" style=\"width:148px\"></list>"
    "<textarea id=\"moves\" style=\"flex:1\"></textarea>"
    "</row>"
    "<span id=\"status\">Open an STL.</span>"
    "<progress id=\"progress\" value=\"0\"></progress>"
    "</window>";

static const char gs_css[] =
    "window{height:100%}"
    "#body{height:230px}"
    "#layers{width:148px}"
    "#moves{flex:1}"
    "#path{flex:1}";

#endif
