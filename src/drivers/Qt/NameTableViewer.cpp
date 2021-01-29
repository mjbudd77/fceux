/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2020 mjbudd77
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
// NameTableViewer.cpp
#include <stdio.h>
#include <stdint.h>

#include <QDir>
#include <QMenu>
#include <QAction>
#include <QMenuBar>
#include <QPainter>
#include <QInputDialog>
#include <QColorDialog>
#include <QMessageBox>

#include "../../types.h"
#include "../../fceu.h"
#include "../../cart.h"
#include "../../ppu.h"
#include "../../ines.h"
#include "../../debug.h"
#include "../../palette.h"

#include "Qt/ConsoleUtilities.h"
#include "Qt/NameTableViewer.h"
#include "Qt/main.h"
#include "Qt/dface.h"
#include "Qt/input.h"
#include "Qt/config.h"
#include "Qt/fceuWrapper.h"

static ppuNameTableViewerDialog_t *nameTableViewWindow = NULL;
static uint8_t palcache[36]; //palette cache
static int NTViewScanline = 0;
static int NTViewSkip = 100;
static int NTViewRefresh = 1;
static int chrchanged = 0;

static int xpos = 0, ypos = 0;
static int attview = 0;
static int hidepal = 0;
static bool drawScrollLines = true;
static bool drawTileGridLines = true;
static bool drawAttrGridLines = false;
static bool redrawtables = true;

//extern int FCEUPPU_GetAttr(int ntnum, int xt, int yt);

// checkerboard tile for attribute view
static const uint8_t ATTRIBUTE_VIEW_TILE[16] = { 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF };


static class NTCache 
{
public:
	NTCache(void) 
		: curr_vnapage(0)
	{
		memset( cache, 0, sizeof(cache) );
	}

	uint8_t* curr_vnapage;
	uint8_t cache[0x400];
} cache[4];

static ppuNameTable_t nameTable[4];

enum NT_MirrorType 
{
	NT_NONE = -1,
	NT_HORIZONTAL, NT_VERTICAL, NT_FOUR_SCREEN,
	NT_SINGLE_SCREEN_TABLE_0, NT_SINGLE_SCREEN_TABLE_1,
	NT_SINGLE_SCREEN_TABLE_2, NT_SINGLE_SCREEN_TABLE_3,
	NT_NUM_MIRROR_TYPES
};
static NT_MirrorType ntmirroring = NT_NONE, oldntmirroring = NT_NONE;

static void initNameTableViewer(void);
//static void ChangeMirroring(void);
//----------------------------------------------------
int openNameTableViewWindow( QWidget *parent )
{
	if ( nameTableViewWindow != NULL )
	{
		return -1;
	}
	initNameTableViewer();

	nameTableViewWindow = new ppuNameTableViewerDialog_t(parent);

	nameTableViewWindow->show();

	return 0;
}
//----------------------------------------------------
ppuNameTableViewerDialog_t::ppuNameTableViewerDialog_t(QWidget *parent)
	: QDialog( parent, Qt::Window )
{
	QHBoxLayout *mainLayout;
	QVBoxLayout *vbox1, *vbox2;
	QHBoxLayout *hbox, *hbox1;
	QGridLayout *grid;
	QGroupBox   *frame;
	QMenuBar *menuBar;
	QMenu *viewMenu, *colorMenu, *subMenu;
	QAction *act, *zoomAct[4], *rateAct[5];
	QActionGroup *group;
	QLabel *lbl;
	QFont   font;
	char stmp[64];
	int useNativeMenuBar;
	fceuDecIntValidtor *validator;

	font.setFamily("Courier New");
	font.setStyle( QFont::StyleNormal );
	font.setStyleHint( QFont::Monospace );

	nameTableViewWindow = this;

	menuBar = new QMenuBar(this);

	// This is needed for menu bar to show up on MacOS
	g_config->getOption( "SDL.UseNativeMenuBar", &useNativeMenuBar );

	menuBar->setNativeMenuBar( useNativeMenuBar ? true : false );

	//-----------------------------------------------------------------------
	// Menu 
	//-----------------------------------------------------------------------
	// View
	viewMenu = menuBar->addMenu(tr("View"));

	// View -> Show Scroll Lines
	act = new QAction(tr("Show Scroll Lines"), this);
	//act->setShortcut(QKeySequence::Open);
	act->setCheckable(true);
	act->setChecked(drawScrollLines);
	act->setStatusTip(tr("Show Scroll Lines"));
	connect(act, SIGNAL(triggered(bool)), this, SLOT(menuScrollLinesChanged(bool)) );
	showScrollLineAct = act;
	
	viewMenu->addAction(act);

	// View -> Show Grid Lines
	act = new QAction(tr("Show Tile Grid"), this);
	//act->setShortcut(QKeySequence::Open);
	act->setCheckable(true);
	act->setChecked(drawTileGridLines);
	act->setStatusTip(tr("Show Tile Grid"));
	connect(act, SIGNAL(triggered(bool)), this, SLOT(menuGridLinesChanged(bool)) );
	showTileGridAct = act;

	viewMenu->addAction(act);

	// View -> Show Attributes
	act = new QAction(tr("Show Attributes"), this);
	//act->setShortcut(QKeySequence::Open);
	act->setCheckable(true);
	act->setChecked(attview);
	act->setStatusTip(tr("Show Attributes"));
	connect(act, SIGNAL(triggered(bool)), this, SLOT(menuAttributesChanged(bool)) );
	showAttributesAct = act;

	viewMenu->addAction(act);

	// View -> Ignore Palette
	act = new QAction(tr("Ignore Palette"), this);
	//act->setShortcut(QKeySequence::Open);
	act->setCheckable(true);
	act->setChecked(attview);
	act->setStatusTip(tr("Ignore Palette"));
	connect(act, SIGNAL(triggered(bool)), this, SLOT(menuIgnPalChanged(bool)) );
	ignPalAct = act;

	viewMenu->addAction(act);

	// View -> Image Zoom
	subMenu = viewMenu->addMenu( tr("Image Zoom"));
	group   = new QActionGroup(this);

	group->setExclusive(true);

	for (int i=0; i<4; i++)
	{
	        char stmp[8];

	        sprintf( stmp, "%ix", i+1 );

	        zoomAct[i] = new QAction(tr(stmp), this);
	        zoomAct[i]->setCheckable(true);

	        group->addAction(zoomAct[i]);
		subMenu->addAction(zoomAct[i]);
	}
	zoomAct[0]->setChecked(true);

	connect(zoomAct[0], SIGNAL(triggered()), this, SLOT(changeZoom1x(void)) );
	connect(zoomAct[1], SIGNAL(triggered()), this, SLOT(changeZoom2x(void)) );
	connect(zoomAct[2], SIGNAL(triggered()), this, SLOT(changeZoom3x(void)) );
	connect(zoomAct[3], SIGNAL(triggered()), this, SLOT(changeZoom4x(void)) );

	viewMenu->addSeparator();

	// View -> Refresh
	act = new QAction(tr("Refresh"), this);
	act->setShortcut( QKeySequence(tr("F5") ) );
	act->setStatusTip(tr("Refresh"));
	connect(act, SIGNAL(triggered()), this, SLOT(forceRefresh()) );

	viewMenu->addAction(act);

	// View -> Auto Refresh Rate
	subMenu = viewMenu->addMenu( tr("Auto Refresh Rate"));
	group   = new QActionGroup(this);

	group->setExclusive(true);

	for (int i=0; i<5; i++)
	{
	        char stmp[8];

		switch ( i )
		{
			case 0:
				strcpy( stmp, "Full" );
			break;
			default:
	        		sprintf( stmp, "1/%i", 0x01 << i );
			break;
		}

	        rateAct[i] = new QAction(tr(stmp), this);
	        rateAct[i]->setCheckable(true);

	        group->addAction(rateAct[i]);
		subMenu->addAction(rateAct[i]);
	}
	rateAct[0]->setChecked( NTViewRefresh == 0  );
	rateAct[1]->setChecked( NTViewRefresh == 1  );
	rateAct[2]->setChecked( NTViewRefresh == 3  );
	rateAct[3]->setChecked( NTViewRefresh == 7  );
	rateAct[4]->setChecked( NTViewRefresh == 19 );

	connect(rateAct[0], SIGNAL(triggered()), this, SLOT(changeRate1(void)) );
	connect(rateAct[1], SIGNAL(triggered()), this, SLOT(changeRate2(void)) );
	connect(rateAct[2], SIGNAL(triggered()), this, SLOT(changeRate4(void)) );
	connect(rateAct[3], SIGNAL(triggered()), this, SLOT(changeRate8(void)) );
	connect(rateAct[4], SIGNAL(triggered()), this, SLOT(changeRate16(void)) );

	// Colors
	colorMenu = menuBar->addMenu(tr("Colors"));

	// Colors -> Tile Selector
	act = new QAction(tr("Tile Selector"), this);
	//act->setShortcut(QKeySequence::Open);
	act->setStatusTip(tr("Tile Selector"));
	connect(act, SIGNAL(triggered()), this, SLOT(setTileSelectorColor()) );
	
	colorMenu->addAction(act);

	// Colors -> Tile Grid
	act = new QAction(tr("Tile Grid"), this);
	//act->setShortcut(QKeySequence::Open);
	act->setStatusTip(tr("Tile Grid"));
	connect(act, SIGNAL(triggered()), this, SLOT(setTileGridColor()) );
	
	colorMenu->addAction(act);

	// Colors -> Attr Grid
	act = new QAction(tr("Attr Grid"), this);
	//act->setShortcut(QKeySequence::Open);
	act->setStatusTip(tr("Attr Grid"));
	connect(act, SIGNAL(triggered()), this, SLOT(setAttrGridColor()) );
	
	colorMenu->addAction(act);

	//-----------------------------------------------------------------------
	// End Menu 
	//-----------------------------------------------------------------------

	setWindowTitle( tr("Name Table Viewer") );

	mainLayout = new QHBoxLayout();

	mainLayout->setMenuBar( menuBar );

	setLayout( mainLayout );

	vbox1  = new QVBoxLayout();
	vbox2  = new QVBoxLayout();
	frame  = new QGroupBox( tr("Tile Info") );
	ntView = new ppuNameTableView_t(this);
	grid   = new QGridLayout();

	scrollArea = new QScrollArea();
	scrollArea->setWidgetResizable(false);
	scrollArea->setSizeAdjustPolicy( QAbstractScrollArea::AdjustToContents );
	scrollArea->setHorizontalScrollBarPolicy( Qt::ScrollBarAsNeeded );
	scrollArea->setVerticalScrollBarPolicy( Qt::ScrollBarAsNeeded );
	scrollArea->setMinimumSize( QSize( 512, 480 ) );

	ntView->setScrollPointer( scrollArea );

	scrollArea->setWidget( ntView );
	mainLayout->addLayout( vbox1, 100 );
	mainLayout->addLayout( vbox2, 1 );

	vbox1->addWidget( scrollArea, 100 );

	vbox2->addWidget( frame );
	frame->setLayout( grid );

	lbl = new QLabel( tr("PPU Addr:") );
	lbl->setFont( font );
	grid->addWidget( lbl, 0, 0, Qt::AlignLeft );

	lbl = new QLabel( tr("Name Table:") );
	lbl->setFont( font );
	grid->addWidget( lbl, 1, 0, Qt::AlignLeft );

	lbl = new QLabel( tr("Location:") );
	lbl->setFont( font );
	grid->addWidget( lbl, 2, 0, Qt::AlignLeft );

	lbl = new QLabel( tr("Tile Index:") );
	lbl->setFont( font );
	grid->addWidget( lbl, 3, 0, Qt::AlignLeft );

	lbl = new QLabel( tr("Tile Addr:") );
	lbl->setFont( font );
	grid->addWidget( lbl, 4, 0, Qt::AlignLeft );

	lbl = new QLabel( tr("Attribute Data:") );
	lbl->setFont( font );
	grid->addWidget( lbl, 5, 0, Qt::AlignLeft );

	lbl = new QLabel( tr("Attribute Addr:") );
	lbl->setFont( font );
	grid->addWidget( lbl, 6, 0, Qt::AlignLeft );

	lbl = new QLabel( tr("Palette Addr:") );
	lbl->setFont( font );
	grid->addWidget( lbl, 7, 0, Qt::AlignLeft );

	ppuAddrLbl = new QLineEdit();
	ppuAddrLbl->setReadOnly(true);
	grid->addWidget( ppuAddrLbl, 0, 1, Qt::AlignLeft );

	nameTableLbl = new QLineEdit();
	nameTableLbl->setReadOnly(true);
	grid->addWidget( nameTableLbl, 1, 1, Qt::AlignLeft );

	tileLocLbl = new QLineEdit();
	tileLocLbl->setReadOnly(true);
	grid->addWidget( tileLocLbl, 2, 1, Qt::AlignLeft );

	tileIdxLbl = new QLineEdit();
	tileIdxLbl->setReadOnly(true);
	grid->addWidget( tileIdxLbl, 3, 1, Qt::AlignLeft );

	tileAddrLbl = new QLineEdit();
	tileAddrLbl->setReadOnly(true);
	grid->addWidget( tileAddrLbl, 4, 1, Qt::AlignLeft );

	attrDataLbl = new QLineEdit();
	attrDataLbl->setReadOnly(true);
	grid->addWidget( attrDataLbl, 5, 1, Qt::AlignLeft );

	attrAddrLbl = new QLineEdit();
	attrAddrLbl->setReadOnly(true);
	grid->addWidget( attrAddrLbl, 6, 1, Qt::AlignLeft );

	palAddrLbl = new QLineEdit();
	palAddrLbl->setReadOnly(true);
	grid->addWidget( palAddrLbl, 7, 1, Qt::AlignLeft );

	showScrollLineCbox = new QCheckBox( tr("Show Scroll Lines") );
	showTileGridCbox   = new QCheckBox( tr("Show Tile Grid") );
	showAttrGridCbox   = new QCheckBox( tr("Show Attr Grid") );
	showAttrbCbox      = new QCheckBox( tr("Show Attributes") );
	ignorePaletteCbox  = new QCheckBox( tr("Ignore Palette") );

	showScrollLineCbox->setChecked( drawScrollLines );
	showTileGridCbox->setChecked( drawTileGridLines );
	showAttrGridCbox->setChecked( drawAttrGridLines );
	showAttrbCbox->setChecked( attview );
	ignorePaletteCbox->setChecked( hidepal );

	vbox2->addWidget( showScrollLineCbox );
	vbox2->addWidget( showTileGridCbox   );
	vbox2->addWidget( showAttrGridCbox   );
	vbox2->addWidget( showAttrbCbox      );
	vbox2->addWidget( ignorePaletteCbox  );

	connect( showScrollLineCbox, SIGNAL(stateChanged(int)), this, SLOT(showScrollLinesChanged(int)));
	connect( showTileGridCbox  , SIGNAL(stateChanged(int)), this, SLOT(showTileGridChanged(int)));
	connect( showAttrGridCbox  , SIGNAL(stateChanged(int)), this, SLOT(showAttrGridChanged(int)));
	connect( showAttrbCbox     , SIGNAL(stateChanged(int)), this, SLOT(showAttrbChanged(int)));
	connect( ignorePaletteCbox , SIGNAL(stateChanged(int)), this, SLOT(ignorePaletteChanged(int)));

	hbox1     = new QHBoxLayout();
	hbox      = new QHBoxLayout();

	vbox1->addLayout( hbox1, 1);

	lbl       = new QLabel( tr("Mirroring Type:") );
	mirrorLbl = new QLabel( tr("Vertical") );
	hbox->addWidget( lbl      , 1, Qt::AlignRight );
	hbox->addWidget( mirrorLbl, 1, Qt::AlignLeft );

	hbox1->addLayout( hbox, 1 );

	hbox     = new QHBoxLayout();

	scanLineEdit = new QLineEdit();
	hbox->addWidget( new QLabel( tr("Display on Scanline:") ), 1, Qt::AlignRight );
	hbox->addWidget( scanLineEdit, 1, Qt::AlignLeft );

	hbox1->addLayout( hbox, 1 );

	validator = new fceuDecIntValidtor( 0, 255, this );
	scanLineEdit->setMaxLength( 3 );
	scanLineEdit->setValidator( validator );
	sprintf( stmp, "%i", NTViewScanline );
	scanLineEdit->setText( tr(stmp) );

	connect( scanLineEdit, SIGNAL(textEdited(const QString &)), this, SLOT(scanLineChanged(const QString &)));

	FCEUD_UpdateNTView( -1, true);
	
	updateTimer  = new QTimer( this );

	connect( updateTimer, &QTimer::timeout, this, &ppuNameTableViewerDialog_t::periodicUpdate );

	updateTimer->start( 33 ); // 30hz

	updateMirrorText();
	updateVisibility();
}
//----------------------------------------------------
ppuNameTableViewerDialog_t::~ppuNameTableViewerDialog_t(void)
{
	updateTimer->stop();
	nameTableViewWindow = NULL;

	printf("Name Table Viewer Window Deleted\n");
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::closeEvent(QCloseEvent *event)
{
	printf("Name Table Viewer Close Window Event\n");
	done(0);
	deleteLater();
	event->accept();
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::closeWindow(void)
{
	printf("Close Window\n");
	done(0);
	deleteLater();
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::periodicUpdate(void)
{
	updateMirrorText();

	if ( redrawtables )
	{
		this->scrollArea->viewport()->update();

		redrawtables = false;
	}
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::changeZoom1x(void)
{
	ntView->setViewScale(1);
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::changeZoom2x(void)
{
	ntView->setViewScale(2);
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::changeZoom3x(void)
{
	ntView->setViewScale(3);
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::changeZoom4x(void)
{
	ntView->setViewScale(4);
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::changeRate( int divider )
{
	if ( divider > 0 )
	{
		int i = 60 / divider;

		NTViewRefresh = (60 / i) - 1;

		//printf("NTViewRefresh: %i \n", NTViewRefresh );
	}
	else
	{
		NTViewRefresh = 1;
	}
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::changeRate1(void)
{
	changeRate(1);
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::changeRate2(void)
{
	changeRate(2);
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::changeRate4(void)
{
	changeRate(4);
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::changeRate8(void)
{
	changeRate(8);
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::changeRate16(void)
{
	changeRate(16);
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::forceRefresh(void)
{
	NTViewSkip = 100;

	FCEUD_UpdateNTView( -1, true);
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::updateVisibility(void)
{

}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::setPropertyLabels( int TileID, int TileX, int TileY, int NameTable, int PPUAddress, int AttAddress, int Attrib, int palAddr )
{
	char stmp[32];

	sprintf( stmp, "%02X", TileID);
	tileIdxLbl->setText( tr(stmp) );

	sprintf( stmp, "%04X", TileID << 4);
	tileAddrLbl->setText( tr(stmp) );

	sprintf( stmp, "%0d, %0d", TileX, TileY);
	tileLocLbl->setText( tr(stmp) );

	sprintf(stmp,"%04X",PPUAddress);
	ppuAddrLbl->setText( tr(stmp) );

	sprintf(stmp,"%1X",NameTable);
	nameTableLbl->setText( tr(stmp) );

	sprintf(stmp,"%02X",Attrib);
	attrDataLbl->setText( tr(stmp) );

	sprintf(stmp,"%04X",AttAddress);
	attrAddrLbl->setText( tr(stmp) );

	sprintf(stmp,"%04X", palAddr );
	palAddrLbl->setText( tr(stmp) );

}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::updateMirrorText(void)
{
	const char *txt = "";

	switch ( ntmirroring )
	{
		default:
		case NT_NONE:
			txt = "None";
		break;
		case NT_HORIZONTAL:
			txt = "Horizontal";
		break;
		case NT_VERTICAL:
			txt = "Vertical";
		break;
		case NT_FOUR_SCREEN:
			txt = "Four Screen";
		break;
		case NT_SINGLE_SCREEN_TABLE_0:
			txt = "Single Screen 0";
		break;
		case NT_SINGLE_SCREEN_TABLE_1:
			txt = "Single Screen 1";
		break;
		case NT_SINGLE_SCREEN_TABLE_2:
			txt = "Single Screen 2";
		break;
		case NT_SINGLE_SCREEN_TABLE_3:
			txt = "Single Screen 3";
		break;
	}

	mirrorLbl->setText( tr(txt) );
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::scanLineChanged( const QString &txt )
{
	std::string s;

	s = txt.toStdString();

	if ( s.size() > 0 )
	{
		NTViewScanline = strtoul( s.c_str(), NULL, 10 );
	}
	else
	{
		NTViewScanline = 0;
	}
	//printf("ScanLine: '%s'  %i\n", s.c_str(), NTViewScanline );
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::menuScrollLinesChanged(bool checked)
{
	drawScrollLines = checked;

	showScrollLineCbox->setChecked( checked );
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::menuGridLinesChanged(bool checked)
{
	drawTileGridLines = checked;

	showTileGridCbox->setChecked( checked );
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::menuAttributesChanged(bool checked)
{
	attview = checked;

	showAttrbCbox->setChecked( checked );
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::menuIgnPalChanged(bool checked)
{
	hidepal = checked;

	ignorePaletteCbox->setChecked( checked );
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::openColorPicker( QColor *c )
{
	int ret;
	QColorDialog dialog( this );

	dialog.setCurrentColor( *c );
	dialog.setOption( QColorDialog::DontUseNativeDialog, true );
	dialog.show();
	ret = dialog.exec();

	if ( ret == QDialog::Accepted )
	{
		//QString colorText;
		//colorText = dialog.selectedColor().name();
		//printf("FG Color string '%s'\n", colorText.toStdString().c_str() );
		//g_config->setOption("SDL.HexEditFgColor", colorText.toStdString().c_str() );
		*c = dialog.selectedColor();
	}
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::setTileSelectorColor(void)
{
	openColorPicker( &ntView->tileSelColor );
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::setTileGridColor(void)
{
	openColorPicker( &ntView->tileGridColor );
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::setAttrGridColor(void)
{
	openColorPicker( &ntView->attrGridColor );
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::showScrollLinesChanged(int state)
{
	drawScrollLines = (state != Qt::Unchecked);

	showScrollLineAct->setChecked( drawScrollLines );
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::showTileGridChanged(int state)
{
	drawTileGridLines = (state != Qt::Unchecked);

	showTileGridAct->setChecked( drawTileGridLines );

	redrawtables = true;
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::showAttrGridChanged(int state)
{
	drawAttrGridLines = (state != Qt::Unchecked);

	//showAttrGridAct->setChecked( drawAttrGridLines );

	redrawtables = true;
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::showAttrbChanged(int state)
{
	attview = (state != Qt::Unchecked);

	showAttributesAct->setChecked( attview );
}
//----------------------------------------------------
void ppuNameTableViewerDialog_t::ignorePaletteChanged(int state)
{
	hidepal = (state != Qt::Unchecked);

	ignPalAct->setChecked( hidepal );
}
//----------------------------------------------------
ppuNameTableView_t::ppuNameTableView_t(QWidget *parent)
	: QWidget(parent)
{
	this->parent = (ppuNameTableViewerDialog_t*)parent;
	this->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
	this->setFocusPolicy(Qt::StrongFocus);
	this->setMouseTracking(true);
	viewScale = 2;
	viewWidth = 256 * 2 * viewScale;
	viewHeight = 240 * 2 * viewScale;
	setMinimumWidth( viewWidth );
	setMinimumHeight( viewHeight );
	resize( viewWidth, viewHeight );

	this->setGeometry(QRect(0,0,viewWidth,viewHeight));

	viewRect= QRect(0, 0, 512, 480);

	selTable   = 0;
	scrollArea = NULL;

	tileSelColor.setRgb(255,255,255);
	tileGridColor.setRgb(255,  0,  0);
	attrGridColor.setRgb(  0,  0,255);
}
//----------------------------------------------------
ppuNameTableView_t::~ppuNameTableView_t(void)
{

}
//----------------------------------------------------
void ppuNameTableView_t::setScrollPointer( QScrollArea *sa )
{
	scrollArea = sa;
}
//----------------------------------------------------
void ppuNameTableView_t::setViewScale( int reqScale )
{
	int prevScale;
	int vw, vh, vx, vy;

	vw = viewRect.width()/2;
	vh = viewRect.height()/2;
	//vx = viewRect.x() + vw;
	//vy = viewRect.y() + vh;
	vx = selTileLoc.x();
	vy = selTileLoc.y();

	prevScale = viewScale;

	viewScale = reqScale;

	if ( viewScale < 1 )
	{
		viewScale = 1;
	}
	else if ( viewScale > 4 )
	{
		viewScale = 4;
	}

	viewWidth = 256 * 2 * viewScale;
	viewHeight = 240 * 2 * viewScale;
	setMinimumWidth( viewWidth );
	setMinimumHeight( viewHeight );
	resize( viewWidth, viewHeight );

	vx = (vx * viewScale) / prevScale;
	vy = (vy * viewScale) / prevScale;

	if ( vx < 0 ) vx = 0;
	if ( vy < 0 ) vy = 0;

	if ( vx > viewWidth  ) vx = viewWidth;
	if ( vy > viewHeight ) vy = viewHeight;

	if ( scrollArea != NULL )
	{
		scrollArea->ensureVisible( vx, vy, vw, vh );
	}

	redrawtables = 1;
}
//----------------------------------------------------
void ppuNameTableView_t::resizeEvent(QResizeEvent *event)
{
	//viewWidth  = event->size().width();
	//viewHeight = event->size().height();

	//printf("%ix%i\n", event->size().width(), event->size().height() );

	redrawtables = 1;
}
//----------------------------------------------------
int ppuNameTableView_t::convertXY2TableTile( int x, int y, int *tableIdxOut, int *tileXout, int *tileYout )
{
	int i, xx, yy, w, h, TileX, TileY, NameTable;
	ppuNameTable_t *tbl = NULL;

	NameTable = 0;

	*tableIdxOut = -1;
	*tileXout = -1;
	*tileYout = -1;

	if ( vnapage[0] == NULL )
	{
		return -1;
	}
	for (i=0; i<4; i++)
	{
		xx = nameTable[i].x;
		yy = nameTable[i].y;
		w = (nameTable[i].w * 256);
		h = (nameTable[i].h * 240);

		if ( (x >= xx) && (x < (xx+w) ) &&
		     (y >= yy) && (y < (yy+h) ) )
		{
			tbl = &nameTable[i];
			NameTable = i;
			break;
		}
	}

	if ( tbl == NULL )
	{
		//printf("Mouse not over a tile\n");
		return -1;
	}

	xx = tbl->x; yy = tbl->y;
	w  = tbl->w;  h = tbl->h;

	if ( (NameTable%2) == 1 )
	{
		TileX = ((x - xx) / (w*8)) + 32;
	}
	else
	{
		TileX = (x - xx) / (w*8);
	}

	if ( (NameTable/2) == 1 )
	{
		TileY = ((y - yy) / (h*8)) + 30;
	}
	else
	{
		TileY = (y - yy) / (h*8);
	}

	*tableIdxOut = NameTable;
	*tileXout    = TileX % 32;
	*tileYout    = TileY % 30;

	return 0;
}
//----------------------------------------------------
int  ppuNameTableView_t::calcTableTileAddr( int NameTable, int TileX, int TileY )
{
	int PPUAddress = 0x2000+(NameTable*0x400)+((TileY%30)*32)+(TileX%32);

	return PPUAddress;
}
//----------------------------------------------------
void ppuNameTableView_t::computeNameTableProperties( int NameTable, int TileX, int TileY )
{
	int TileID, PPUAddress, AttAddress, Attrib, palAddr;

	if ( vnapage[0] == NULL )
	{
		return;
	}

	PPUAddress = 0x2000+(NameTable*0x400)+((TileY%30)*32)+(TileX%32);

	TileID = vnapage[(PPUAddress>>10)&0x3][PPUAddress&0x3FF];

	AttAddress = 0x23C0 | (PPUAddress & 0x0C00) | ((PPUAddress >> 4) & 0x38) | ((PPUAddress >> 2) & 0x07);
	Attrib = vnapage[(AttAddress>>10)&0x3][AttAddress&0x3FF];
	//Attrib = (Attrib >> ((PPUAddress&2) | ((PPUAddress&64)>>4))) & 0x3;

	//palAddr = 0x3F00 + ( FCEUPPU_GetAttr( NameTable, TileX, TileY ) * 4 );
	palAddr = 0x3F00 + ( (Attrib >> ((PPUAddress&2) | ((PPUAddress&64)>>4))) & 0x3 ) * 4;

	//printf("NT:%i Tile X/Y : %i/%i \n", NameTable, TileX, TileY );

	if ( parent )
	{
		parent->setPropertyLabels( TileID, TileX, TileY, NameTable, PPUAddress, AttAddress, Attrib, palAddr );
	}
}
//----------------------------------------------------
void ppuNameTableView_t::keyPressEvent(QKeyEvent *event)
{
	if ( event->key() == Qt::Key_Minus )
	{
		setViewScale( viewScale-1 );

		event->accept();
	}
	else if ( event->key() == Qt::Key_Plus )
	{
		setViewScale( viewScale+1 );

		event->accept();
	}
	else if ( event->key() == Qt::Key_Up )
	{
		int y = selTile.y();

		y--;
		
		if ( y < 0 )
		{
			if ( selTable < 2 )
			{
				selTable += 2;
			}	
			else 
			{
				selTable -= 2;
			}	
			y = 29;
		}
		selTile.setY( y );

		computeNameTableProperties( selTable, selTile.x(), selTile.y() );

		ensureVis = true;

		event->accept();
	}
	else if ( event->key() == Qt::Key_Down )
	{
		int y = selTile.y();

		y++;
		
		if ( y >= 30 )
		{
			if ( selTable < 2 )
			{
				selTable += 2;
			}	
			else 
			{
				selTable -= 2;
			}	
			y = 0;
		}
		selTile.setY( y );

		computeNameTableProperties( selTable, selTile.x(), selTile.y() );

		ensureVis = true;

		event->accept();
	}
	else if ( event->key() == Qt::Key_Left )
	{
		int x = selTile.x();

		x--;
		
		if ( x < 0 )
		{
			if ( selTable % 2 )
			{
				selTable -= 1;
			}	
			else 
			{
				selTable += 1;
			}	
			x = 31;
		}
		selTile.setX( x );

		computeNameTableProperties( selTable, selTile.x(), selTile.y() );

		ensureVis = true;

		event->accept();
	}
	else if ( event->key() == Qt::Key_Right )
	{
		int x = selTile.x();

		x++;
		
		if ( x >= 32 )
		{
			if ( selTable % 2 )
			{
				selTable -= 1;
			}	
			else 
			{
				selTable += 1;
			}	
			x = 0;
		}
		selTile.setX( x );

		computeNameTableProperties( selTable, selTile.x(), selTile.y() );

		ensureVis = true;

		event->accept();
	}
}
//----------------------------------------------------
void ppuNameTableView_t::mouseMoveEvent(QMouseEvent *event)
{
	//printf("MouseMove: (%i,%i) \n", event->pos().x(), event->pos().y() );
}
//----------------------------------------------------------------------------
void ppuNameTableView_t::mousePressEvent(QMouseEvent * event)
{
	int tIdx, tx, ty;

	convertXY2TableTile( event->pos().x(), event->pos().y(), &tIdx, &tx, &ty );

	if ( event->button() == Qt::LeftButton )
	{
		//printf(" %i  %i  %i \n", tIdx, tx, ty );
		selTable = tIdx;
		selTile.setX( tx );
		selTile.setY( ty );

		computeNameTableProperties( tIdx, tx, ty );
	}
	else if ( event->button() == Qt::RightButton )
	{
	}
}
//----------------------------------------------------
void ppuNameTableView_t::paintEvent(QPaintEvent *event)
{
	ppuNameTable_t *nt;
	int n,i,j,ii,jj,w,h,x,y,xx,yy,ww,hh;
	QPainter painter(this);
	QColor scanLineColor(255,255,255);
	QPen   pen;
	
	viewRect = event->rect();

	w = viewWidth / (256*2);
  	h = viewHeight / (240*2);

	//printf("(%i,%i) %ix%i\n", event->rect().x(), event->rect().y(), event->rect().width(), event->rect().height() );

	xx = 0; yy = 0;

	for (n=0; n<4; n++)
	{
		nt = &nameTable[n];

		nt->w = w; nt->h = h;
		
		nt->x = xx = (n%2) * (viewWidth / 2);
		nt->y = yy = (n/2) * (viewHeight / 2);

		for (j=0; j<30; j++)
		{
			jj = (j*8);

			for (i=0; i<32; i++)
			{
				ii = (i*8);

				nt->tile[j][i].x = xx+(ii*w);
				nt->tile[j][i].y = yy+(jj*h);

				for (y=0; y<8; y++)
				{
					for (x=0; x<8; x++)
					{
						painter.fillRect( xx+(ii+x)*w, yy+(jj+y)*h, w, h, nt->tile[j][i].pixel[y][x].color );
					}
				}
			}
		}
		if ( drawScrollLines )
		{
			ww = nt->w * 256;
			hh = nt->h * 240;

			painter.setPen( scanLineColor );

			if ( (xpos >= xx) && (xpos < (xx+ww)) )
			{
				painter.drawLine( xpos, yy, xpos, yy + hh );
			}

			if ( (ypos >= yy) && (ypos < (yy+hh)) )
			{
				painter.drawLine( xx, ypos, xx + ww, ypos );
			}
		}
		if ( drawTileGridLines )
		{
			painter.setPen( tileGridColor );

			for (x=0; x<256; x+=8)
			{
				painter.drawLine( xx + x*w, yy, xx + x*w, yy + hh );
			}
			for (y=0; y<240; y+=8)
			{
				painter.drawLine( xx, yy + y*h, xx + ww, yy + y*h );
			}
		}
		if ( drawAttrGridLines )
		{
			painter.setPen( attrGridColor );

			for (x=0; x<256; x+=16)
			{
				painter.drawLine( xx + x*w, yy, xx + x*w, yy + hh );
			}
			for (y=0; y<240; y+=16)
			{
				painter.drawLine( xx, yy + y*h, xx + ww, yy + y*h );
			}
		}
	}

	xx = nameTable[ selTable ].tile[ selTile.y() ][ selTile.x() ].x;
	yy = nameTable[ selTable ].tile[ selTile.y() ][ selTile.x() ].y;

	selTileLoc.setX( xx );
	selTileLoc.setY( yy );

	pen.setWidth( 3 );
	pen.setColor( QColor(  0,  0,  0) );
	painter.setPen( pen );

	painter.drawRect( xx, yy, w*8, h*8 );

	pen.setWidth( 1 );
	pen.setColor( tileSelColor );
	painter.setPen( pen );

	painter.drawRect( xx, yy, w*8, h*8 );

	if ( ensureVis && scrollArea )
	{
		scrollArea->ensureVisible( selTileLoc.x(), selTileLoc.y(), 128, 128 );

		ensureVis = false;
	}
	computeNameTableProperties( selTable, selTile.x(), selTile.y() );

}
//----------------------------------------------------
static void initNameTableViewer(void)
{
	//clear cache
	memset(palcache,0,32);

	// forced palette (e.g. for debugging nametables when palettes are all-black)
	palcache[(8*4)+0] = 0x0F;
	palcache[(8*4)+1] = 0x00;
	palcache[(8*4)+2] = 0x10;
	palcache[(8*4)+3] = 0x20;

}
//----------------------------------------------------
//static void ChangeMirroring(void)
//{
//	switch (ntmirroring)
//	{
//		case NT_HORIZONTAL:
//			vnapage[0] = vnapage[1] = &NTARAM[0x000];
//			vnapage[2] = vnapage[3] = &NTARAM[0x400];
//			break;
//		case NT_VERTICAL:
//			vnapage[0] = vnapage[2] = &NTARAM[0x000];
//			vnapage[1] = vnapage[3] = &NTARAM[0x400];
//			break;
//		case NT_FOUR_SCREEN:
//			vnapage[0] = &NTARAM[0x000];
//			vnapage[1] = &NTARAM[0x400];
//			if(ExtraNTARAM)
//			{
//				vnapage[2] = ExtraNTARAM;
//				vnapage[3] = ExtraNTARAM + 0x400;
//			}
//			break;
//		case NT_SINGLE_SCREEN_TABLE_0:
//			vnapage[0] = vnapage[1] = vnapage[2] = vnapage[3] = &NTARAM[0x000];
//			break;
//		case NT_SINGLE_SCREEN_TABLE_1:
//			vnapage[0] = vnapage[1] = vnapage[2] = vnapage[3] = &NTARAM[0x400];
//			break;
//		case NT_SINGLE_SCREEN_TABLE_2:
//			if(ExtraNTARAM)
//				vnapage[0] = vnapage[1] = vnapage[2] = vnapage[3] = ExtraNTARAM;
//			break;
//		case NT_SINGLE_SCREEN_TABLE_3:
//			if(ExtraNTARAM)
//				vnapage[0] = vnapage[1] = vnapage[2] = vnapage[3] = ExtraNTARAM + 0x400;
//			break;
//		default:
//		case NT_NONE:
//			break;
//	}
//	return;
//}
//----------------------------------------------------
inline void DrawChr( ppuNameTableTile_t *tile, const uint8_t *chr, int pal)
{
	int y, x, tmp, index=0, p=0;
	uint8 chr0, chr1;
	//uint8 *table = &VPage[0][0]; //use the background table
	//pbitmap += 3*

	for (y = 0; y < 8; y++) { //todo: use index for y?
		chr0 = chr[index];
		chr1 = chr[index+8];
		tmp=7;
		for (x = 0; x < 8; x++) { //todo: use tmp for x?
			p = (chr0>>tmp)&1;
			p |= ((chr1>>tmp)&1)<<1;
			p = palcache[p+(pal*4)];
			tmp--;

			tile->pixel[y][x].color.setBlue( palo[p].b );
			tile->pixel[y][x].color.setGreen( palo[p].g );
			tile->pixel[y][x].color.setRed( palo[p].r );
		}
		index++;
		//pbitmap += (NTWIDTH*3)-24;
	}
	//index+=8;
	//pbitmap -= (((PALETTEBITWIDTH>>2)<<3)-24);
}
//----------------------------------------------------
static void DrawNameTable(int scanline, int ntnum, bool invalidateCache) 
{
	NTCache &c = cache[ntnum];
	uint8_t *tablecache = c.cache;

	uint8_t *table = vnapage[ntnum];
	if (table == NULL)
	{
		table = vnapage[ntnum&1];
	}

	int a, ptable=0;
	
	if (PPU[0]&0x10){ //use the correct pattern table based on this bit
		ptable=0x1000;
	}

	bool invalid = invalidateCache;
	//if we werent asked to invalidate the cache, maybe we need to invalidate it anyway due to vnapage changing
	if (!invalid)
	{
		invalid = (c.curr_vnapage != vnapage[ntnum]);
	}
	c.curr_vnapage = vnapage[ntnum];
	
	//HACK: never cache anything
	invalid = true;

	for (int y=0;y<30;y++)
	{
		for (int x=0;x<32;x++)
		{
			int ntaddr = (y*32)+x;
			int attraddr = 0x3C0+((y>>2)<<3)+(x>>2);
			if (invalid
				|| (table[ntaddr] != tablecache[ntaddr]) 
				|| (table[attraddr] != tablecache[attraddr])) 
			{
				int temp = (((y&2)<<1)+(x&2));
				a = (table[attraddr] & (3<<temp)) >> temp;
				
				//the commented out code below is all allegedly equivalent to the single line above:
				//tmpx = x>>2;
				//tmpy = y>>2;
				//a = 0x3C0+(tmpy*8)+tmpx;
				//if((((x>>1)&1) == 0) && (((y>>1)&1) == 0)) a = table[a]&0x3;
				//if((((x>>1)&1) == 1) && (((y>>1)&1) == 0)) a = (table[a]&0xC)>>2;
				//if((((x>>1)&1) == 0) && (((y>>1)&1) == 1)) a = (table[a]&0x30)>>4;
				//if((((x>>1)&1) == 1) && (((y>>1)&1) == 1)) a = (table[a]&0xC0)>>6;

				int chr = table[ntaddr]*16;

				//test.. instead of pretending that the nametable is a screen at 0,0 we pretend that it is at the current xscroll and yscroll
				//int xpos = ((RefreshAddr & 0x400) >> 2) | ((RefreshAddr & 0x1F) << 3) | XOffset;
				//int ypos = ((RefreshAddr & 0x3E0) >> 2) | ((RefreshAddr & 0x7000) >> 12); 
				//if(RefreshAddr & 0x800) ypos += 240;
				//int refreshaddr = (xpos/8+x)+(ypos/8+y)*32;

				int refreshaddr = (x)+(y)*32;

				a = FCEUPPU_GetAttr(ntnum,x,y);
				if (hidepal) a = 8;

				const uint8* chrp = FCEUPPU_GetCHR(ptable+chr,refreshaddr);
				if (attview) chrp = ATTRIBUTE_VIEW_TILE;

				//a good way to do it:
				DrawChr( &nameTable[ntnum].tile[y][x], chrp, a);

				tablecache[ntaddr] = table[ntaddr];
				tablecache[attraddr] = table[attraddr];
				//one could comment out the line above...
				//since there are so many fewer attribute values than NT values, it might be best just to refresh the whole attr table below with the memcpy

				//obviously this whole scheme of nt cache doesnt work if an mmc5 game is playing tricks with the attribute table
			}
		}
	}
}
//----------------------------------------------------
void FCEUD_UpdateNTView(int scanline, bool drawall) 
{
	if (nameTableViewWindow == 0)
	{
		return;
	}
	if ( (scanline != -1) && (scanline != NTViewScanline) )
	{
		return;
	}

	ppu_getScroll(xpos,ypos);

	if (NTViewSkip < NTViewRefresh)
	{
		NTViewSkip++;
		return;
	}
	NTViewSkip = 0;

	if (chrchanged)
	{
		drawall = 1;
	}

	//update palette only if required
	if (memcmp(palcache,PALRAM,32) != 0) 
	{
		memcpy(palcache,PALRAM,32);
		drawall = 1; //palette has changed, so redraw all
	}

	if ( vnapage[0] == NULL )
	{
		return;
	}
	 
	ntmirroring = NT_NONE;
	if (vnapage[0] == vnapage[1])ntmirroring = NT_HORIZONTAL;
	if (vnapage[0] == vnapage[2])ntmirroring = NT_VERTICAL;
	if ((vnapage[0] != vnapage[1]) && (vnapage[0] != vnapage[2]))ntmirroring = NT_FOUR_SCREEN;

	if ((vnapage[0] == vnapage[1]) && (vnapage[1] == vnapage[2]) && (vnapage[2] == vnapage[3]))
	{ 
		if(vnapage[0] == &NTARAM[0x000])ntmirroring = NT_SINGLE_SCREEN_TABLE_0;
		if(vnapage[0] == &NTARAM[0x400])ntmirroring = NT_SINGLE_SCREEN_TABLE_1;
		if(vnapage[0] == ExtraNTARAM)ntmirroring = NT_SINGLE_SCREEN_TABLE_2;
		if(vnapage[0] == ExtraNTARAM+0x400)ntmirroring = NT_SINGLE_SCREEN_TABLE_3;
	}

	if (oldntmirroring != ntmirroring)
	{
		oldntmirroring = ntmirroring;
	}

	for (int i=0;i<4;i++)
	{
		DrawNameTable(scanline,i,drawall);
	}

	chrchanged = 0;
	redrawtables = true;
	return;	
}
//----------------------------------------------------
