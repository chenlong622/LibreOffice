/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <osl/diagnose.h>

#include <com/sun/star/uno/Any.hxx>
#include <com/sun/star/uno/Sequence.hxx>

#include <svtools/colorcfg.hxx>

#include <global.hxx>
#include <viewopti.hxx>
#include <sc.hrc>
#include <scmod.hxx>
#include <miscuno.hxx>

using namespace utl;
using namespace com::sun::star::uno;



void ScGridOptions::SetDefaults()
{
    *this = ScGridOptions();

    //  grid defaults differ now between the apps
    //  therefore, enter here in its own right (all in 1/100mm)

    if ( ScOptionsUtil::IsMetricSystem() )
    {
        nFldDrawX = 1000;   // 1cm
        nFldDrawY = 1000;
    }
    else
    {
        nFldDrawX = 1270;   // 0,5"
        nFldDrawY = 1270;
    }
    nFldDivisionX = 1;
    nFldDivisionY = 1;
}

bool ScGridOptions::operator==( const ScGridOptions& rCpy ) const
{
    return (   nFldDrawX        == rCpy.nFldDrawX
            && nFldDivisionX    == rCpy.nFldDivisionX
            && nFldDrawY        == rCpy.nFldDrawY
            && nFldDivisionY    == rCpy.nFldDivisionY
            && bUseGridsnap     == rCpy.bUseGridsnap
            && bSynchronize     == rCpy.bSynchronize
            && bGridVisible     == rCpy.bGridVisible
            && bEqualGrid       == rCpy.bEqualGrid );
}

ScViewRenderingOptions::ScViewRenderingOptions()
    : msColorSchemeName(u"Default"_ustr)
    , maDocumentColor(ScModule::get()->GetColorConfig().GetColorValue(svtools::DOCCOLOR).nColor)
{
}

bool ScViewRenderingOptions::operator==(const ScViewRenderingOptions& rOther) const
{
    return msColorSchemeName == rOther.msColorSchemeName &&
           maDocumentColor == rOther.maDocumentColor;
}

ScViewOptions::ScViewOptions()
{
    SetDefaults();
}

ScViewOptions::ScViewOptions( const ScViewOptions& rCpy )
{
    *this = rCpy;
}

ScViewOptions::~ScViewOptions()
{
}

void ScViewOptions::SetDefaults()
{
    aOptArr[sal_Int32(sc::ViewOption::FORMULAS)] = false;
    aOptArr[sal_Int32(sc::ViewOption::SYNTAX)] = false;
    aOptArr[sal_Int32(sc::ViewOption::HELPLINES)] = false;
    aOptArr[sal_Int32(sc::ViewOption::GRID_ONTOP)] = false;
    aOptArr[sal_Int32(sc::ViewOption::NOTES)] = true;
    aOptArr[sal_Int32(sc::ViewOption::NOTEAUTHOR)] = true;
    aOptArr[sal_Int32(sc::ViewOption::FORMULAS_MARKS)] = false;
    aOptArr[sal_Int32(sc::ViewOption::NULLVALS)] = true;
    aOptArr[sal_Int32(sc::ViewOption::VSCROLL)] = true;
    aOptArr[sal_Int32(sc::ViewOption::HSCROLL)] = true;
    aOptArr[sal_Int32(sc::ViewOption::TABCONTROLS)] = true;
    aOptArr[sal_Int32(sc::ViewOption::OUTLINER)] = true;
    aOptArr[sal_Int32(sc::ViewOption::HEADER)] = true;
    aOptArr[sal_Int32(sc::ViewOption::GRID)] = true;
    aOptArr[sal_Int32(sc::ViewOption::ANCHOR)] = true;
    aOptArr[sal_Int32(sc::ViewOption::PAGEBREAKS)] = true;
    aOptArr[sal_Int32(sc::ViewOption::SUMMARY)] = true;
    aOptArr[sal_Int32(sc::ViewOption::COPY_SHEET)] = false;
    aOptArr[sal_Int32(sc::ViewOption::THEMEDCURSOR)] = false;

    aModeArr[sal_Int32(sc::ViewObjectType::OLE)]  = VOBJ_MODE_SHOW;
    aModeArr[sal_Int32(sc::ViewObjectType::CHART)] = VOBJ_MODE_SHOW;
    aModeArr[sal_Int32(sc::ViewObjectType::DRAW)] = VOBJ_MODE_SHOW;

    aGridCol = svtools::ColorConfig().GetColorValue( svtools::CALCGRID ).nColor;

    aGridOpt.SetDefaults();
}

Color const & ScViewOptions::GetGridColor( OUString* pStrName ) const
{
    if ( pStrName )
        *pStrName = aGridColName;

    return aGridCol;
}

ScViewOptions& ScViewOptions::operator=(const ScViewOptions& rCpy) = default;

bool ScViewOptions::operator==( const ScViewOptions& rOpt ) const
{
    bool bEqual = true;
    sal_uInt16  i;

    for ( i=0; i<MAX_OPT && bEqual; i++ )  bEqual = (aOptArr [i] == rOpt.aOptArr[i]);
    for ( i=0; i<MAX_TYPE && bEqual; i++ ) bEqual = (aModeArr[i] == rOpt.aModeArr[i]);

    bEqual = bEqual && (aGridCol       == rOpt.aGridCol);
    bEqual = bEqual && (aGridColName   == rOpt.aGridColName);
    bEqual = bEqual && (aGridOpt       == rOpt.aGridOpt);

    return bEqual;
}

std::unique_ptr<SvxGridItem> ScViewOptions::CreateGridItem() const
{
    std::unique_ptr<SvxGridItem> pItem(new SvxGridItem( SID_ATTR_GRID_OPTIONS ));

    pItem->SetFieldDrawX      ( aGridOpt.GetFieldDrawX() );
    pItem->SetFieldDivisionX  ( aGridOpt.GetFieldDivisionX() );
    pItem->SetFieldDrawY      ( aGridOpt.GetFieldDrawY() );
    pItem->SetFieldDivisionY  ( aGridOpt.GetFieldDivisionY() );
    pItem->SetUseGridSnap   ( aGridOpt.GetUseGridSnap() );
    pItem->SetSynchronize   ( aGridOpt.GetSynchronize() );
    pItem->SetGridVisible   ( aGridOpt.GetGridVisible() );
    pItem->SetEqualGrid     ( aGridOpt.GetEqualGrid() );

    return pItem;
}

//      ScTpViewItem - data for the ViewOptions TabPage

ScTpViewItem::ScTpViewItem( const ScViewOptions& rOpt )
    :   SfxPoolItem ( SID_SCVIEWOPTIONS ),
        theOptions  ( rOpt )
{
}

ScTpViewItem::~ScTpViewItem()
{
}

bool ScTpViewItem::operator==( const SfxPoolItem& rItem ) const
{
    assert(SfxPoolItem::operator==(rItem));

    const ScTpViewItem& rPItem = static_cast<const ScTpViewItem&>(rItem);

    return ( theOptions == rPItem.theOptions );
}

ScTpViewItem* ScTpViewItem::Clone( SfxItemPool * ) const
{
    return new ScTpViewItem( *this );
}

//  Config Item containing view options

constexpr OUStringLiteral CFGPATH_LAYOUT = u"Office.Calc/Layout";

#define SCLAYOUTOPT_GRIDLINES       0
#define SCLAYOUTOPT_GRIDCOLOR       1
#define SCLAYOUTOPT_PAGEBREAK       2
#define SCLAYOUTOPT_GUIDE           3
#define SCLAYOUTOPT_COLROWHDR       4
#define SCLAYOUTOPT_HORISCROLL      5
#define SCLAYOUTOPT_VERTSCROLL      6
#define SCLAYOUTOPT_SHEETTAB        7
#define SCLAYOUTOPT_OUTLINE         8
#define SCLAYOUTOPT_GRID_ONCOLOR    9
#define SCLAYOUTOPT_SUMMARY         10
#define SCLAYOUTOPT_THEMEDCURSOR    11

constexpr OUStringLiteral CFGPATH_DISPLAY = u"Office.Calc/Content/Display";

#define SCDISPLAYOPT_FORMULA        0
#define SCDISPLAYOPT_ZEROVALUE      1
#define SCDISPLAYOPT_NOTETAG        2
#define SCDISPLAYOPT_NOTEAUTHOR     3
#define SCDISPLAYOPT_FORMULAMARK    4
#define SCDISPLAYOPT_VALUEHI        5
#define SCDISPLAYOPT_ANCHOR         6
#define SCDISPLAYOPT_OBJECTGRA      7
#define SCDISPLAYOPT_CHART          8
#define SCDISPLAYOPT_DRAWING        9

constexpr OUStringLiteral CFGPATH_GRID = u"Office.Calc/Grid";

#define SCGRIDOPT_RESOLU_X          0
#define SCGRIDOPT_RESOLU_Y          1
#define SCGRIDOPT_SUBDIV_X          2
#define SCGRIDOPT_SUBDIV_Y          3
#define SCGRIDOPT_SNAPTOGRID        4
#define SCGRIDOPT_SYNCHRON          5
#define SCGRIDOPT_VISIBLE           6
#define SCGRIDOPT_SIZETOGRID        7

Sequence<OUString> ScViewCfg::GetLayoutPropertyNames()
{
    return {u"Line/GridLine"_ustr,            // SCLAYOUTOPT_GRIDLINES
            u"Line/GridLineColor"_ustr,       // SCLAYOUTOPT_GRIDCOLOR
            u"Line/PageBreak"_ustr,           // SCLAYOUTOPT_PAGEBREAK
            u"Line/Guide"_ustr,               // SCLAYOUTOPT_GUIDE
            u"Window/ColumnRowHeader"_ustr,   // SCLAYOUTOPT_COLROWHDR
            u"Window/HorizontalScroll"_ustr,  // SCLAYOUTOPT_HORISCROLL
            u"Window/VerticalScroll"_ustr,    // SCLAYOUTOPT_VERTSCROLL
            u"Window/SheetTab"_ustr,          // SCLAYOUTOPT_SHEETTAB
            u"Window/OutlineSymbol"_ustr,     // SCLAYOUTOPT_OUTLINE
            u"Line/GridOnColoredCells"_ustr,  // SCLAYOUTOPT_GRID_ONCOLOR;
            u"Window/SearchSummary"_ustr,     // SCLAYOUTOPT_SUMMARY
            u"Window/ThemedCursor"_ustr};     // SCLAYOUTOPT_THEMEDCURSOR
}

Sequence<OUString> ScViewCfg::GetDisplayPropertyNames()
{
    return {u"Formula"_ustr,                  // SCDISPLAYOPT_FORMULA
            u"ZeroValue"_ustr,                // SCDISPLAYOPT_ZEROVALUE
            u"NoteTag"_ustr,                  // SCDISPLAYOPT_NOTETAG
            u"NoteAuthor"_ustr,               // SCDISPLAYOPT_NOTEAUTHOR
            u"FormulaMark"_ustr,              // SCDISPLAYOPT_FORMULAMARK
            u"ValueHighlighting"_ustr,        // SCDISPLAYOPT_VALUEHI
            u"Anchor"_ustr,                   // SCDISPLAYOPT_ANCHOR
            u"ObjectGraphic"_ustr,            // SCDISPLAYOPT_OBJECTGRA
            u"Chart"_ustr,                    // SCDISPLAYOPT_CHART
            u"DrawingObject"_ustr};           // SCDISPLAYOPT_DRAWING;
}

Sequence<OUString> ScViewCfg::GetGridPropertyNames()
{
    const bool bIsMetric = ScOptionsUtil::IsMetricSystem();

    return {(bIsMetric ? u"Resolution/XAxis/Metric"_ustr
                       : u"Resolution/XAxis/NonMetric"_ustr),   // SCGRIDOPT_RESOLU_X
            (bIsMetric ? u"Resolution/YAxis/Metric"_ustr
                       : u"Resolution/YAxis/NonMetric"_ustr),   // SCGRIDOPT_RESOLU_Y
             u"Subdivision/XAxis"_ustr,                                   // SCGRIDOPT_SUBDIV_X
             u"Subdivision/YAxis"_ustr,                                   // SCGRIDOPT_SUBDIV_Y
             u"Option/SnapToGrid"_ustr,                                   // SCGRIDOPT_SNAPTOGRID
             u"Option/Synchronize"_ustr,                                  // SCGRIDOPT_SYNCHRON
             u"Option/VisibleGrid"_ustr,                                  // SCGRIDOPT_VISIBLE
             u"Option/SizeToGrid"_ustr};                                  // SCGRIDOPT_SIZETOGRID;
}

ScViewCfg::ScViewCfg() :
    aLayoutItem( CFGPATH_LAYOUT ),
    aDisplayItem( CFGPATH_DISPLAY ),
    aGridItem( CFGPATH_GRID )
{
    Sequence<OUString> aNames = GetLayoutPropertyNames();
    Sequence<Any> aValues = aLayoutItem.GetProperties(aNames);
    aLayoutItem.EnableNotification(aNames);
    const Any* pValues = aValues.getConstArray();
    OSL_ENSURE(aValues.getLength() == aNames.getLength(), "GetProperties failed");
    if(aValues.getLength() == aNames.getLength())
    {
        for(int nProp = 0; nProp < aNames.getLength(); nProp++)
        {
            OSL_ENSURE(pValues[nProp].hasValue(), "property value missing");
            if(pValues[nProp].hasValue())
            {
                switch(nProp)
                {
                    case SCLAYOUTOPT_GRIDCOLOR:
                    {
                        Color aColor;
                        if ( pValues[nProp] >>= aColor )
                            SetGridColor( aColor, OUString() );
                        break;
                    }
                    case SCLAYOUTOPT_GRIDLINES:
                        SetOption( sc::ViewOption::GRID, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                        break;
                    case SCLAYOUTOPT_GRID_ONCOLOR:
                        SetOption( sc::ViewOption::GRID_ONTOP, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                        break;
                    case SCLAYOUTOPT_PAGEBREAK:
                        SetOption( sc::ViewOption::PAGEBREAKS, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                        break;
                    case SCLAYOUTOPT_GUIDE:
                        SetOption( sc::ViewOption::HELPLINES, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                        break;
                    case SCLAYOUTOPT_COLROWHDR:
                        SetOption( sc::ViewOption::HEADER, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                        break;
                    case SCLAYOUTOPT_HORISCROLL:
                        SetOption( sc::ViewOption::HSCROLL, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                        break;
                    case SCLAYOUTOPT_VERTSCROLL:
                        SetOption( sc::ViewOption::VSCROLL, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                        break;
                    case SCLAYOUTOPT_SHEETTAB:
                        SetOption( sc::ViewOption::TABCONTROLS, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                        break;
                    case SCLAYOUTOPT_OUTLINE:
                        SetOption( sc::ViewOption::OUTLINER, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                        break;
                    case SCLAYOUTOPT_SUMMARY:
                        SetOption( sc::ViewOption::SUMMARY, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                        break;
                    case SCLAYOUTOPT_THEMEDCURSOR:
                        SetOption( sc::ViewOption::THEMEDCURSOR, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                        break;
                }
            }
        }
    }
    aLayoutItem.SetCommitLink( LINK( this, ScViewCfg, LayoutCommitHdl ) );

    aDisplayItem.EnableNotification(GetDisplayPropertyNames());
    ReadDisplayCfg();
    aDisplayItem.SetCommitLink( LINK( this, ScViewCfg, DisplayCommitHdl ) );
    aDisplayItem.SetNotifyLink( LINK( this, ScViewCfg, DisplayNotifyHdl ) );

    aGridItem.EnableNotification(GetGridPropertyNames());
    ReadGridCfg();
    aGridItem.SetCommitLink( LINK( this, ScViewCfg, GridCommitHdl ) );
    aGridItem.SetNotifyLink( LINK( this, ScViewCfg, GridNotifyHdl ) );
}

IMPL_LINK_NOARG(ScViewCfg, LayoutCommitHdl, ScLinkConfigItem&, void)
{
    Sequence<OUString> aNames = GetLayoutPropertyNames();
    Sequence<Any> aValues(aNames.getLength());
    Any* pValues = aValues.getArray();

    for(int nProp = 0; nProp < aNames.getLength(); nProp++)
    {
        switch(nProp)
        {
            case SCLAYOUTOPT_GRIDCOLOR:
                pValues[nProp] <<= GetGridColor();
                break;
            case SCLAYOUTOPT_GRIDLINES:
                pValues[nProp] <<= GetOption( sc::ViewOption::GRID );
                break;
            case SCLAYOUTOPT_GRID_ONCOLOR:
                pValues[nProp] <<= GetOption( sc::ViewOption::GRID_ONTOP );
                break;
            case SCLAYOUTOPT_PAGEBREAK:
                pValues[nProp] <<= GetOption( sc::ViewOption::PAGEBREAKS );
                break;
            case SCLAYOUTOPT_GUIDE:
                pValues[nProp] <<= GetOption( sc::ViewOption::HELPLINES );
                break;
            case SCLAYOUTOPT_COLROWHDR:
                pValues[nProp] <<= GetOption( sc::ViewOption::HEADER );
                break;
            case SCLAYOUTOPT_HORISCROLL:
                pValues[nProp] <<= GetOption( sc::ViewOption::HSCROLL );
                break;
            case SCLAYOUTOPT_VERTSCROLL:
                pValues[nProp] <<= GetOption( sc::ViewOption::VSCROLL );
                break;
            case SCLAYOUTOPT_SHEETTAB:
                pValues[nProp] <<= GetOption( sc::ViewOption::TABCONTROLS );
                break;
            case SCLAYOUTOPT_OUTLINE:
                pValues[nProp] <<= GetOption( sc::ViewOption::OUTLINER );
                break;
            case SCLAYOUTOPT_SUMMARY:
                pValues[nProp] <<= GetOption( sc::ViewOption::SUMMARY );
                break;
            case SCLAYOUTOPT_THEMEDCURSOR:
                pValues[nProp] <<= GetOption( sc::ViewOption::THEMEDCURSOR );
                break;
        }
    }
    aLayoutItem.PutProperties(aNames, aValues);
}

void ScViewCfg::ReadDisplayCfg()
{
    const Sequence<OUString> aNames = GetDisplayPropertyNames();
    const Sequence<Any> aValues = aDisplayItem.GetProperties(aNames);
    OSL_ENSURE(aValues.getLength() == aNames.getLength(), "GetProperties failed");
    if (aValues.getLength() != aNames.getLength())
        return;

    sal_Int32 nIntVal = 0;

    const Any* pValues = aValues.getConstArray();
    for(int nProp = 0; nProp < aNames.getLength(); nProp++)
    {
        OSL_ENSURE(pValues[nProp].hasValue(), "property value missing");
        if(pValues[nProp].hasValue())
        {
            switch(nProp)
            {
                case SCDISPLAYOPT_FORMULA:
                    SetOption(sc::ViewOption::FORMULAS, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                    break;
                case SCDISPLAYOPT_ZEROVALUE:
                    SetOption(sc::ViewOption::NULLVALS, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                    break;
                case SCDISPLAYOPT_NOTETAG:
                    SetOption(sc::ViewOption::NOTES, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                    break;
                case SCDISPLAYOPT_NOTEAUTHOR:
                    SetOption(sc::ViewOption::NOTEAUTHOR, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                    break;
                case SCDISPLAYOPT_FORMULAMARK:
                    SetOption(sc::ViewOption::FORMULAS_MARKS, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                    break;
                case SCDISPLAYOPT_VALUEHI:
                    SetOption(sc::ViewOption::SYNTAX, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                    break;
                case SCDISPLAYOPT_ANCHOR:
                    SetOption(sc::ViewOption::ANCHOR, ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                    break;
                case SCDISPLAYOPT_OBJECTGRA:
                    if ( pValues[nProp] >>= nIntVal )
                    {
                        //#i80528# adapt to new range eventually
                        if(sal_Int32(VOBJ_MODE_HIDE) < nIntVal) nIntVal = sal_Int32(VOBJ_MODE_SHOW);

                        SetObjMode(sc::ViewObjectType::OLE, static_cast<ScVObjMode>(nIntVal));
                    }
                    break;
                case SCDISPLAYOPT_CHART:
                    if ( pValues[nProp] >>= nIntVal )
                    {
                        //#i80528# adapt to new range eventually
                        if(sal_Int32(VOBJ_MODE_HIDE) < nIntVal) nIntVal = sal_Int32(VOBJ_MODE_SHOW);

                        SetObjMode(sc::ViewObjectType::CHART, static_cast<ScVObjMode>(nIntVal));
                    }
                    break;
                case SCDISPLAYOPT_DRAWING:
                    if ( pValues[nProp] >>= nIntVal )
                    {
                        //#i80528# adapt to new range eventually
                        if(sal_Int32(VOBJ_MODE_HIDE) < nIntVal) nIntVal = sal_Int32(VOBJ_MODE_SHOW);

                        SetObjMode(sc::ViewObjectType::DRAW, static_cast<ScVObjMode>(nIntVal));
                    }
                    break;
            }
        }
    }
}

IMPL_LINK_NOARG(ScViewCfg, DisplayNotifyHdl, ScLinkConfigItem&, void)
{
    ReadDisplayCfg();
}

IMPL_LINK_NOARG(ScViewCfg, DisplayCommitHdl, ScLinkConfigItem&, void)
{
    Sequence<OUString> aNames = GetDisplayPropertyNames();
    Sequence<Any> aValues(aNames.getLength());
    Any* pValues = aValues.getArray();

    for(int nProp = 0; nProp < aNames.getLength(); nProp++)
    {
        switch(nProp)
        {
            case SCDISPLAYOPT_FORMULA:
                pValues[nProp] <<= GetOption(sc::ViewOption::FORMULAS);
                break;
            case SCDISPLAYOPT_ZEROVALUE:
                pValues[nProp] <<= GetOption(sc::ViewOption::NULLVALS);
                break;
            case SCDISPLAYOPT_NOTETAG:
                pValues[nProp] <<= GetOption(sc::ViewOption::NOTES);
                break;
            case SCDISPLAYOPT_NOTEAUTHOR:
                pValues[nProp] <<= GetOption(sc::ViewOption::NOTEAUTHOR);
                break;
            case SCDISPLAYOPT_FORMULAMARK:
                pValues[nProp] <<= GetOption(sc::ViewOption::FORMULAS_MARKS);
                break;
            case SCDISPLAYOPT_VALUEHI:
                pValues[nProp] <<= GetOption(sc::ViewOption::SYNTAX);
                break;
            case SCDISPLAYOPT_ANCHOR:
                pValues[nProp] <<= GetOption(sc::ViewOption::ANCHOR);
                break;
            case SCDISPLAYOPT_OBJECTGRA:
                pValues[nProp] <<= static_cast<sal_Int32>(GetObjMode(sc::ViewObjectType::OLE));
                break;
            case SCDISPLAYOPT_CHART:
                pValues[nProp] <<= static_cast<sal_Int32>(GetObjMode(sc::ViewObjectType::CHART));
                break;
            case SCDISPLAYOPT_DRAWING:
                pValues[nProp] <<= static_cast<sal_Int32>(GetObjMode(sc::ViewObjectType::DRAW));
                break;
        }
    }
    aDisplayItem.PutProperties(aNames, aValues);
}

void ScViewCfg::ReadGridCfg()
{
    const Sequence<OUString> aNames = GetGridPropertyNames();
    const Sequence<Any> aValues = aGridItem.GetProperties(aNames);
    OSL_ENSURE(aValues.getLength() == aNames.getLength(), "GetProperties failed");
    if (aValues.getLength() != aNames.getLength())
        return;

    sal_Int32 nIntVal = 0;

    ScGridOptions aGrid = GetGridOptions();     //TODO: initialization necessary?
    const Any* pValues = aValues.getConstArray();

    for(int nProp = 0; nProp < aNames.getLength(); nProp++)
    {
        OSL_ENSURE(pValues[nProp].hasValue(), "property value missing");
        if(pValues[nProp].hasValue())
        {
            switch(nProp)
            {
                case SCGRIDOPT_RESOLU_X:
                    if (pValues[nProp] >>= nIntVal) aGrid.SetFieldDrawX( nIntVal );
                    break;
                case SCGRIDOPT_RESOLU_Y:
                    if (pValues[nProp] >>= nIntVal) aGrid.SetFieldDrawY( nIntVal );
                    break;
                case SCGRIDOPT_SUBDIV_X:
                    if (pValues[nProp] >>= nIntVal) aGrid.SetFieldDivisionX( nIntVal );
                    break;
                case SCGRIDOPT_SUBDIV_Y:
                    if (pValues[nProp] >>= nIntVal) aGrid.SetFieldDivisionY( nIntVal );
                    break;
                case SCGRIDOPT_SNAPTOGRID:
                    aGrid.SetUseGridSnap( ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                    break;
                case SCGRIDOPT_SYNCHRON:
                    aGrid.SetSynchronize( ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                    break;
                case SCGRIDOPT_VISIBLE:
                    aGrid.SetGridVisible( ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                    break;
                case SCGRIDOPT_SIZETOGRID:
                    aGrid.SetEqualGrid( ScUnoHelpFunctions::GetBoolFromAny( pValues[nProp] ) );
                    break;
            }
        }
    }

    SetGridOptions( aGrid );
}

IMPL_LINK_NOARG(ScViewCfg, GridNotifyHdl, ScLinkConfigItem&, void)
{
    ReadGridCfg();
}

IMPL_LINK_NOARG(ScViewCfg, GridCommitHdl, ScLinkConfigItem&, void)
{
    const ScGridOptions& rGrid = GetGridOptions();

    Sequence<OUString> aNames = GetGridPropertyNames();
    Sequence<Any> aValues(aNames.getLength());
    Any* pValues = aValues.getArray();

    for(int nProp = 0; nProp < aNames.getLength(); nProp++)
    {
        switch(nProp)
        {
            case SCGRIDOPT_RESOLU_X:
                pValues[nProp] <<= static_cast<sal_Int32>(rGrid.GetFieldDrawX());
                break;
            case SCGRIDOPT_RESOLU_Y:
                pValues[nProp] <<= static_cast<sal_Int32>(rGrid.GetFieldDrawY());
                break;
            case SCGRIDOPT_SUBDIV_X:
                pValues[nProp] <<= static_cast<sal_Int32>(rGrid.GetFieldDivisionX());
                break;
            case SCGRIDOPT_SUBDIV_Y:
                pValues[nProp] <<= static_cast<sal_Int32>(rGrid.GetFieldDivisionY());
                break;
            case SCGRIDOPT_SNAPTOGRID:
                pValues[nProp] <<= rGrid.GetUseGridSnap();
                break;
            case SCGRIDOPT_SYNCHRON:
                pValues[nProp] <<= rGrid.GetSynchronize();
                break;
            case SCGRIDOPT_VISIBLE:
                pValues[nProp] <<= rGrid.GetGridVisible();
                break;
            case SCGRIDOPT_SIZETOGRID:
                pValues[nProp] <<= rGrid.GetEqualGrid();
                break;
        }
    }
    aGridItem.PutProperties(aNames, aValues);
}

void ScViewCfg::SetOptions( const ScViewOptions& rNew )
{
    *static_cast<ScViewOptions*>(this) = rNew;
    aLayoutItem.SetModified();
    aDisplayItem.SetModified();
    aGridItem.SetModified();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
