from pathlib import Path
import re, sys, html
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import landscape, letter
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import inch
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, Image, PageBreak, HRFlowable, KeepTogether

ROOT = Path.cwd()
PAGE = landscape(letter)
MARGIN = 0.55 * inch
styles = getSampleStyleSheet()
styles.add(ParagraphStyle(name='DocTitle', parent=styles['Title'], fontName='Helvetica-Bold', fontSize=19, leading=23, spaceAfter=14, textColor=colors.HexColor('#17365D')))
for level, size in [(1,15),(2,12.5),(3,11)]:
    styles.add(ParagraphStyle(name=f'H{level}x', parent=styles['Heading2'], fontName='Helvetica-Bold', fontSize=size, leading=size+3, spaceBefore=11, spaceAfter=6, textColor=colors.HexColor('#17365D')))
styles.add(ParagraphStyle(name='Bodyx', parent=styles['BodyText'], fontName='Helvetica', fontSize=8.7, leading=11.2, spaceAfter=4))
styles.add(ParagraphStyle(name='Bulletx', parent=styles['BodyText'], fontName='Helvetica', fontSize=8.7, leading=11.2, leftIndent=14, firstLineIndent=-9, spaceAfter=2))
styles.add(ParagraphStyle(name='Captionx', parent=styles['BodyText'], fontName='Helvetica-Oblique', fontSize=7.5, leading=9, spaceAfter=7, alignment=TA_LEFT))

def markup(text):
    text = html.escape(text)
    text = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', text)
    text = re.sub(r'`(.+?)`', r'<font face="Courier">\1</font>', text)
    return text

def add_table(rows, story):
    clean=[]
    for row in rows:
        cells=[c.strip() for c in row.strip().strip('|').split('|')]
        if cells and all(re.fullmatch(r':?-{3,}:?', c.replace(' ','')) for c in cells): continue
        clean.append([Paragraph(markup(c), styles['Bodyx']) for c in cells])
    if not clean: return
    n=max(len(r) for r in clean)
    clean=[r+[Paragraph('',styles['Bodyx'])]*(n-len(r)) for r in clean]
    width=(PAGE[0]-2*MARGIN)/n
    tbl=Table(clean, colWidths=[width]*n, repeatRows=1, hAlign='LEFT')
    tbl.setStyle(TableStyle([
      ('BACKGROUND',(0,0),(-1,0),colors.HexColor('#17365D')),('TEXTCOLOR',(0,0),(-1,0),colors.white),
      ('FONTNAME',(0,0),(-1,0),'Helvetica-Bold'),('VALIGN',(0,0),(-1,-1),'TOP'),
      ('GRID',(0,0),(-1,-1),0.25,colors.HexColor('#AAB7C4')),('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,colors.HexColor('#F3F6F8')]),
      ('LEFTPADDING',(0,0),(-1,-1),5),('RIGHTPADDING',(0,0),(-1,-1),5),('TOPPADDING',(0,0),(-1,-1),4),('BOTTOMPADDING',(0,0),(-1,-1),4),
    ]))
    story += [tbl, Spacer(1,8)]

def render(md_path, out_path):
    lines=md_path.read_text().splitlines(); story=[]; i=0
    while i < len(lines):
        line=lines[i]
        if not line.strip(): i+=1; continue
        if line.startswith('|'):
            rows=[]
            while i<len(lines) and lines[i].startswith('|'):
                rows.append(lines[i]); i+=1
            add_table(rows,story); continue
        m=re.fullmatch(r'!\[(.*?)\]\((.*?)\)', line.strip())
        if m:
            target=(md_path.parent/m.group(2)).resolve()
            if target.exists():
                from PIL import Image as PILImage
                with PILImage.open(target) as im: w,h=im.size
                maxw=PAGE[0]-2*MARGIN; maxh=PAGE[1]-2*MARGIN-0.45*inch
                scale=min(maxw/w,maxh/h)
                story += [Image(str(target),width=w*scale,height=h*scale), Spacer(1,5), Paragraph(markup(m.group(1)),styles['Captionx'])]
            else: story.append(Paragraph(f'<i>Missing image: {markup(m.group(2))}</i>',styles['Bodyx']))
            i+=1; continue
        if re.fullmatch(r'-{3,}',line.strip()): story += [Spacer(1,4), HRFlowable(width='100%', thickness=.5,color=colors.HexColor('#B7C3D0')),Spacer(1,6)]; i+=1; continue
        hm=re.match(r'^(#{1,4})\s+(.*)',line)
        if hm:
            level=len(hm.group(1)); txt=hm.group(2)
            sty='DocTitle' if level==1 else f'H{min(level-1,3)}x'
            story.append(Paragraph(markup(txt),styles[sty])); i+=1; continue
        if line.startswith('- '): story.append(Paragraph('• '+markup(line[2:]),styles['Bulletx'])); i+=1; continue
        story.append(Paragraph(markup(line),styles['Bodyx'])); i+=1
    def footer(canvas, doc):
        canvas.saveState(); canvas.setFont('Helvetica',7); canvas.setFillColor(colors.HexColor('#607080'))
        canvas.drawString(MARGIN,0.32*inch,md_path.stem); canvas.drawRightString(PAGE[0]-MARGIN,0.32*inch,f'Page {doc.page}')
        canvas.restoreState()
    doc=SimpleDocTemplate(str(out_path),pagesize=PAGE,leftMargin=MARGIN,rightMargin=MARGIN,topMargin=0.48*inch,bottomMargin=0.52*inch,title=md_path.stem)
    doc.build(story,onFirstPage=footer,onLaterPages=footer)

for src,dst in [(ROOT/'Documents/Requirement Docs.md',ROOT/'output/pdf/requirements.pdf'),(ROOT/'Documents/Testing_Document.md',ROOT/'output/pdf/unit-testing-plan.pdf')]:
    render(src,dst)
    print(dst)
