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

#include <memory>
#include <imp_op.hxx>

#include <filter/msfilter/countryid.hxx>

#include <scitems.hxx>

#include <o3tl/safeint.hxx>
#include <sfx2/docfile.hxx>
#include <svx/svxids.hrc>
#include <svl/numformat.hxx>
#include <comphelper/configuration.hxx>
#include <sal/log.hxx>

#include <sfx2/objsh.hxx>
#include <tools/urlobj.hxx>
#include <docuno.hxx>
#include <docsh.hxx>

#include <formulacell.hxx>
#include <document.hxx>
#include <globstr.hrc>
#include <scresid.hxx>
#include <global.hxx>
#include <olinetab.hxx>
#include <stlpool.hxx>
#include <viewopti.hxx>
#include <docoptio.hxx>
#include <scextopt.hxx>
#include <unonames.hxx>
#include <paramisc.hxx>
#include <colrowst.hxx>
#include <otlnbuff.hxx>
#include <xistyle.hxx>

#include <namebuff.hxx>
#include <xltools.hxx>
#include <xltable.hxx>
#include <xltracer.hxx>
#include <xihelper.hxx>
#include <xipage.hxx>
#include <xiview.hxx>
#include <xiescher.hxx>
#include <xicontent.hxx>

#include <excform.hxx>
#include <documentimport.hxx>

#if defined(_WIN32)
#include <math.h>
#endif

using namespace ::com::sun::star;

ImportTyp::ImportTyp(ScDocument& rDoc, rtl_TextEncoding eQ)
    : eQuellChar(eQ)
    , rD(rDoc)

{
}

ImportTyp::~ImportTyp()
{
}

ImportExcel::ImportExcel( XclImpRootData& rImpData, SvStream& rStrm ):
    ImportTyp( rImpData.mrDoc, rImpData.meTextEnc ),
    XclImpRoot( rImpData ),
    maStrm( rStrm, GetRoot() ),
    aIn( maStrm ),
    maScOleSize( ScAddress::INITIALIZE_INVALID ),
    pColOutlineBuff(nullptr),
    pRowOutlineBuff(nullptr),
    pColRowBuff(nullptr),
    mpLastFormula(nullptr),
    mnLastRefIdx( 0 ),
    mnIxfeIndex( 0 ),
    mnLastRecId(0),
    mbBiff2HasXfs(false),
    mbBiff2HasXfsValid(false),
    mbFuzzing(comphelper::IsFuzzing())
{
    nBdshtTab = 0;

    // fill in root data - after new's without root as parameter
    pExcRoot = &GetOldRoot();
    pExcRoot->pIR = this;   // ExcRoot -> XclImpRoot
    pExcRoot->eDateiTyp = BiffX;
    pExcRoot->pExtSheetBuff.reset( new ExtSheetBuffer( pExcRoot ) );   //&aExtSheetBuff;
    pExcRoot->pShrfmlaBuff.reset( new SharedFormulaBuffer( pExcRoot ) );     //&aShrfrmlaBuff;
    pExcRoot->pExtNameBuff.reset( new ExtNameBuff ( *this ) );

    pOutlineListBuffer.reset(new XclImpOutlineListBuffer);

    // from Biff8 on
    pFormConv.reset(new ExcelToSc( GetRoot() ));
    pExcRoot->pFmlaConverter = pFormConv.get();

    bTabTruncated = false;

    // Excel document per Default on 31.12.1899, accords to Excel settings with 1.1.1900
    ScDocOptions aOpt = rD.GetDocOptions();
    aOpt.SetDate( 30, 12, 1899 );
    rD.SetDocOptions( aOpt );
    rD.GetFormatTable()->ChangeNullDate( 30, 12, 1899 );

    ScDocOptions aDocOpt( rD.GetDocOptions() );
    aDocOpt.SetIgnoreCase( true );              // always in Excel
    aDocOpt.SetFormulaRegexEnabled( false );    // regular expressions? what's that?
    aDocOpt.SetFormulaWildcardsEnabled( true ); // Excel uses wildcard expressions
    aDocOpt.SetLookUpColRowNames( false );      // default: no natural language refs
    rD.SetDocOptions( aDocOpt );
}

ImportExcel::~ImportExcel()
{
    GetDoc().SetSrcCharSet( GetTextEncoding() );

    pOutlineListBuffer.reset();

    pFormConv.reset();
}

void ImportExcel::SetLastFormula( SCCOL nCol, SCROW nRow, double fVal, sal_uInt16 nXF, ScFormulaCell* pCell )
{
    LastFormulaMapType::iterator it = maLastFormulaCells.find(nCol);
    if (it == maLastFormulaCells.end())
    {
        std::pair<LastFormulaMapType::iterator, bool> r =
            maLastFormulaCells.emplace(nCol, LastFormula());
        it = r.first;
    }

    it->second.mnCol = nCol;
    it->second.mnRow = nRow;
    it->second.mpCell = pCell;
    it->second.mfValue = fVal;
    it->second.mnXF = nXF;

    mpLastFormula = &it->second;
}

void ImportExcel::ReadFileSharing()
{
    sal_uInt16 nRecommendReadOnly, nPasswordHash;
    nRecommendReadOnly = maStrm.ReaduInt16();
    nPasswordHash = maStrm.ReaduInt16();

    if((nRecommendReadOnly == 0) && (nPasswordHash == 0))
        return;

    GetMedium().GetItemSet().Put( SfxBoolItem( SID_DOC_READONLY, true ) );

    if( ScDocShell* pShell = GetDocShell() )
    {
        if( nRecommendReadOnly != 0 )
            pShell->SetLoadReadonly( true );
        if( nPasswordHash != 0 )
            pShell->SetModifyPasswordHash( nPasswordHash );
    }
}

sal_uInt16 ImportExcel::ReadXFIndex( const ScAddress& rScPos, bool bBiff2 )
{
    sal_uInt16 nXFIdx = 0;
    if( bBiff2 )
    {
        /*  #i71453# On first call, check if the file contains XF records (by
            trying to access the first XF with index 0). If there are no XFs,
            the explicit formatting information contained in each cell record
            will be used instead. */
        if( !mbBiff2HasXfsValid )
        {
            mbBiff2HasXfsValid = true;
            mbBiff2HasXfs = GetXFBuffer().GetXF( 0 ) != nullptr;
        }
        // read formatting information (includes the XF identifier)
        sal_uInt8 nFlags1, nFlags2, nFlags3;
        nFlags1 = maStrm.ReaduInt8();
        nFlags2 = maStrm.ReaduInt8();
        nFlags3 = maStrm.ReaduInt8();
        /*  If the file contains XFs, extract and set the XF identifier,
            otherwise get the explicit formatting. */
        if( mbBiff2HasXfs )
        {
            nXFIdx = ::extract_value< sal_uInt16 >( nFlags1, 0, 6 );
            /*  If the identifier is equal to 63, then the real identifier is
                contained in the preceding IXFE record (stored in mnBiff2XfId). */
            if( nXFIdx == 63 )
                nXFIdx = mnIxfeIndex;
        }
        else
        {
            /*  Let the XclImpXF class do the conversion of the imported
                formatting. The XF buffer is empty, therefore will not do any
                conversion based on the XF index later on. */
            XclImpXF::ApplyPatternForBiff2CellFormat( GetRoot(), rScPos, nFlags1, nFlags2, nFlags3 );
        }
    }
    else
        nXFIdx = aIn.ReaduInt16();
    return nXFIdx;
}

void ImportExcel::ReadDimensions()
{
    XclRange aXclUsedArea;
    if( (maStrm.GetRecId() == EXC_ID2_DIMENSIONS) || (GetBiff() <= EXC_BIFF5) )
    {
        maStrm >> aXclUsedArea;
        if( (aXclUsedArea.GetColCount() > 1) && (aXclUsedArea.GetRowCount() > 1) )
        {
            // Excel stores first unused row/column index
            --aXclUsedArea.maLast.mnCol;
            --aXclUsedArea.maLast.mnRow;
            // create the Calc range
            SCTAB nScTab = GetCurrScTab();
            ScRange& rScUsedArea = GetExtDocOptions().GetOrCreateTabSettings( nScTab ).maUsedArea;
            GetAddressConverter().ConvertRange( rScUsedArea, aXclUsedArea, nScTab, nScTab, false );
            // if any error occurs in ConvertRange(), rScUsedArea keeps untouched
        }
    }
    else
    {
        sal_uInt32 nXclRow1 = maStrm.ReaduInt32();
        sal_uInt32 nXclRow2 = maStrm.ReaduInt32();
        aXclUsedArea.maFirst.mnCol = maStrm.ReaduInt16();
        aXclUsedArea.maLast.mnCol = maStrm.ReaduInt16();
        if( nXclRow2 != 0 && (nXclRow1 < nXclRow2) && (aXclUsedArea.GetColCount() > 1) &&
            (nXclRow1 <= o3tl::make_unsigned( GetScMaxPos().Row() )) )
        {
            // Excel stores first unused row/column index
            --nXclRow2;
            aXclUsedArea.maLast.mnCol = o3tl::sanitizing_dec(aXclUsedArea.maLast.mnCol);
            // convert row indexes to 16-bit values
            aXclUsedArea.maFirst.mnRow = static_cast< sal_uInt16 >( nXclRow1 );
            aXclUsedArea.maLast.mnRow = limit_cast< sal_uInt16 >( nXclRow2, aXclUsedArea.maFirst.mnRow, SAL_MAX_UINT16 );
            // create the Calc range
            SCTAB nScTab = GetCurrScTab();
            ScRange& rScUsedArea = GetExtDocOptions().GetOrCreateTabSettings( nScTab ).maUsedArea;
            GetAddressConverter().ConvertRange( rScUsedArea, aXclUsedArea, nScTab, nScTab, false );
            // if any error occurs in ConvertRange(), rScUsedArea keeps untouched
        }
    }
}

void ImportExcel::ReadBlank()
{
    XclAddress aXclPos;
    aIn >> aXclPos;

    ScAddress aScPos( ScAddress::UNINITIALIZED );
    if( GetAddressConverter().ConvertAddress( aScPos, aXclPos, GetCurrScTab(), true ) )
    {
        sal_uInt16 nXFIdx = ReadXFIndex( aScPos, maStrm.GetRecId() == EXC_ID2_BLANK );

        GetXFRangeBuffer().SetBlankXF( aScPos, nXFIdx );
    }
}

void ImportExcel::ReadInteger()
{
    XclAddress aXclPos;
    maStrm >> aXclPos;

    ScAddress aScPos( ScAddress::UNINITIALIZED );
    if( GetAddressConverter().ConvertAddress( aScPos, aXclPos, GetCurrScTab(), true ) )
    {
        sal_uInt16 nXFIdx = ReadXFIndex( aScPos, true );
        sal_uInt16 nValue;
        nValue = maStrm.ReaduInt16();

        GetXFRangeBuffer().SetXF( aScPos, nXFIdx );
        GetDocImport().setNumericCell(aScPos, nValue);
    }
}

void ImportExcel::ReadNumber()
{
    XclAddress aXclPos;
    maStrm >> aXclPos;

    ScAddress aScPos( ScAddress::UNINITIALIZED );
    if( GetAddressConverter().ConvertAddress( aScPos, aXclPos, GetCurrScTab(), true ) )
    {
        sal_uInt16 nXFIdx = ReadXFIndex( aScPos, maStrm.GetRecId() == EXC_ID2_NUMBER );
        double fValue;
        fValue = maStrm.ReadDouble();

        GetXFRangeBuffer().SetXF( aScPos, nXFIdx );
        GetDocImport().setNumericCell(aScPos, fValue);
    }
}

void ImportExcel::ReadLabel()
{
    XclAddress aXclPos;
    maStrm >> aXclPos;

    ScAddress aScPos( ScAddress::UNINITIALIZED );
    if( !GetAddressConverter().ConvertAddress( aScPos, aXclPos, GetCurrScTab(), true ) )
        return;

    /*  Record ID   BIFF    XF type     String type
        0x0004      2-7     3 byte      8-bit length, byte string
        0x0004      8       3 byte      16-bit length, unicode string
        0x0204      2-7     2 byte      16-bit length, byte string
        0x0204      8       2 byte      16-bit length, unicode string */
    bool bBiff2 = maStrm.GetRecId() == EXC_ID2_LABEL;
    sal_uInt16 nXFIdx = ReadXFIndex( aScPos, bBiff2 );
    XclStrFlags nFlags = (bBiff2 && (GetBiff() <= EXC_BIFF5)) ? XclStrFlags::EightBitLength : XclStrFlags::NONE;
    XclImpString aString;

    // #i63105# use text encoding from FONT record
    rtl_TextEncoding eOldTextEnc = GetTextEncoding();
    if( const XclImpFont* pFont = GetXFBuffer().GetFont( nXFIdx ) )
        SetTextEncoding( pFont->GetFontEncoding() );
    aString.Read( maStrm, nFlags );
    SetTextEncoding( eOldTextEnc );

    GetXFRangeBuffer().SetXF( aScPos, nXFIdx );
    XclImpStringHelper::SetToDocument(GetDocImport(), aScPos, GetRoot(), aString, nXFIdx);
}

void ImportExcel::ReadBoolErr()
{
    XclAddress aXclPos;
    maStrm >> aXclPos;

    ScAddress aScPos( ScAddress::UNINITIALIZED );
    if( !GetAddressConverter().ConvertAddress( aScPos, aXclPos, GetCurrScTab(), true ) )
        return;

    sal_uInt16 nXFIdx = ReadXFIndex( aScPos, maStrm.GetRecId() == EXC_ID2_BOOLERR );
    sal_uInt8 nValue, nType;
    nValue = maStrm.ReaduInt8();
    nType = maStrm.ReaduInt8();

    if( nType == EXC_BOOLERR_BOOL )
        GetXFRangeBuffer().SetBoolXF( aScPos, nXFIdx );
    else
        GetXFRangeBuffer().SetXF( aScPos, nXFIdx );

    double fValue;
    std::unique_ptr<ScTokenArray> pScTokArr = ErrorToFormula( nType != EXC_BOOLERR_BOOL, nValue, fValue );
    ScFormulaCell* pCell = pScTokArr
        ? new ScFormulaCell(rD, aScPos, std::move(pScTokArr))
        : new ScFormulaCell(rD, aScPos);
    pCell->SetHybridDouble( fValue );
    GetDocImport().setFormulaCell(aScPos, pCell);
}

void ImportExcel::ReadRk()
{
    XclAddress aXclPos;
    maStrm >> aXclPos;

    ScAddress aScPos( ScAddress::UNINITIALIZED );
    if( GetAddressConverter().ConvertAddress( aScPos, aXclPos, GetCurrScTab(), true ) )
    {
        sal_uInt16 nXFIdx = ReadXFIndex( aScPos, false );
        sal_Int32 nRk;
        nRk = maStrm.ReadInt32();

        GetXFRangeBuffer().SetXF( aScPos, nXFIdx );
        GetDocImport().setNumericCell(aScPos, XclTools::GetDoubleFromRK(nRk));
    }
}

void ImportExcel::Window1()
{
    GetDocViewSettings().ReadWindow1( maStrm );
}

void ImportExcel::Row25()
{
    sal_uInt16  nRow, nRowHeight;

    nRow = aIn.ReaduInt16();
    aIn.Ignore( 4 );

    if( !GetRoot().GetDoc().ValidRow( nRow ) )
        return;

    nRowHeight = aIn.ReaduInt16();  // specify direct in Twips
    aIn.Ignore( 2 );

    if( GetBiff() == EXC_BIFF2 )
    {// -------------------- BIFF2
        pColRowBuff->SetHeight( nRow, nRowHeight );
    }
    else
    {// -------------------- BIFF5
        sal_uInt16  nGrbit;

        aIn.Ignore( 2 );   // reserved
        nGrbit = aIn.ReaduInt16();

        sal_uInt8 nLevel = ::extract_value< sal_uInt8 >( nGrbit, 0, 3 );
        pRowOutlineBuff->SetLevel( nRow, nLevel, ::get_flag( nGrbit, EXC_ROW_COLLAPSED ) );
        pColRowBuff->SetRowSettings( nRow, nRowHeight, nGrbit );
    }
}

void ImportExcel::Bof2()
{
    sal_uInt16 nSubType;
    maStrm.DisableDecryption();
    maStrm.Ignore( 2 );
    nSubType = maStrm.ReaduInt16();

    if( nSubType == 0x0020 )        // Chart
        pExcRoot->eDateiTyp = Biff2C;
    else if( nSubType == 0x0040 )   // Macro
        pExcRoot->eDateiTyp = Biff2M;
    else                            // #i51490# Excel interprets invalid indexes as worksheet
        pExcRoot->eDateiTyp = Biff2;
}

void ImportExcel::Eof()
{
    //  POST: cannot be called after an invalid table!
    EndSheet();
    IncCurrScTab();
}

void ImportExcel::SheetPassword()
{
    if (GetRoot().GetBiff() != EXC_BIFF8)
        return;

    GetRoot().GetSheetProtectBuffer().ReadPasswordHash( aIn, GetCurrScTab() );
}

void ImportExcel::Externsheet()
{
    OUString aUrl, aTabName;
    bool bSameWorkBook;
    OUString aEncodedUrl( aIn.ReadByteString( false ) );
    XclImpUrlHelper::DecodeUrl( aUrl, aTabName, bSameWorkBook, *pExcRoot->pIR, aEncodedUrl );
    mnLastRefIdx = pExcRoot->pExtSheetBuff->Add( aUrl, aTabName, bSameWorkBook );
}

void ImportExcel:: WinProtection()
{
    if (GetRoot().GetBiff() != EXC_BIFF8)
        return;

    GetRoot().GetDocProtectBuffer().ReadWinProtect( aIn );
}

void ImportExcel::Columndefault()
{// Default Cell Attributes
    sal_uInt16 nColMic = aIn.ReaduInt16();
    sal_uInt16 nColMac = aIn.ReaduInt16();

    OSL_ENSURE( aIn.GetRecLeft() == static_cast<std::size_t>(nColMac - nColMic) * 3 + 2,
                "ImportExcel::Columndefault - wrong record size" );

    if (nColMac == 0)
    {
        SAL_WARN("sc", "dodgy column defaults");
        return;
    }

    assert(nColMac > 0 && "coverity 2023.12.2");

    nColMac--;

    if( nColMac > rD.MaxCol() )
        nColMac = static_cast<sal_uInt16>(rD.MaxCol());

    for( sal_uInt16 nCol = nColMic ; nCol <= nColMac ; nCol++ )
    {
        sal_uInt8 nOpt0 = aIn.ReaduInt8();
        aIn.Ignore( 2 );   // only 0. Attribute-Byte used

        if( nOpt0 & 0x80 )  // Col hidden?
            pColRowBuff->HideCol( nCol );
    }
}

void ImportExcel::Array25()
{
    sal_uInt16 nFormLen;
    sal_uInt16 nFirstRow = aIn.ReaduInt16();
    sal_uInt16 nLastRow = aIn.ReaduInt16();
    sal_uInt8 nFirstCol = aIn.ReaduInt8();
    sal_uInt8 nLastCol = aIn.ReaduInt8();

    if( GetBiff() == EXC_BIFF2 )
    {//                     BIFF2
        aIn.Ignore( 1 );
        nFormLen = aIn.ReaduInt8();
    }
    else
    {//                     BIFF5
        aIn.Ignore( 6 );
        nFormLen = aIn.ReaduInt16();
    }

    std::unique_ptr<ScTokenArray> pResult;

    if (GetRoot().GetDoc().ValidColRow(nLastCol, nLastRow))
    {
        // the read mark is now on the formula, length in nFormLen

        pFormConv->Reset( ScAddress( static_cast<SCCOL>(nFirstCol),
                    static_cast<SCROW>(nFirstRow), GetCurrScTab() ) );
        pFormConv->Convert(pResult, maStrm, nFormLen, true);

        SAL_WARN_IF(!pResult, "sc", "*ImportExcel::Array25(): ScTokenArray is NULL!");
    }

    if (pResult)
    {
        ScDocumentImport& rDoc = GetDocImport();
        ScRange aArrayRange(nFirstCol, nFirstRow, GetCurrScTab(), nLastCol, nLastRow, GetCurrScTab());
        rDoc.setMatrixCells(aArrayRange, *pResult, formula::FormulaGrammar::GRAM_ENGLISH_XL_A1);
    }
}

void ImportExcel::Rec1904()
{
    sal_uInt16  n1904;

    n1904 = aIn.ReaduInt16();

    if( n1904 )
    {// 1904 date system
        ScDocOptions aOpt = rD.GetDocOptions();
        aOpt.SetDate( 1, 1, 1904 );
        rD.SetDocOptions( aOpt );
        rD.GetFormatTable()->ChangeNullDate( 1, 1, 1904 );
    }
}

void ImportExcel::Externname25()
{
    sal_uInt32      nRes;
    sal_uInt16      nOpt;

    nOpt = aIn.ReaduInt16();
    nRes = aIn.ReaduInt32();

    aIn.ReadByteString( false ); // name

    if( ( nOpt & 0x0001 ) || ( ( nOpt & 0xFFFE ) == 0x0000 ) )
    {// external name
        pExcRoot->pExtNameBuff->AddName( mnLastRefIdx );
    }
    else if( nOpt & 0x0010 )
    {// ole link
        pExcRoot->pExtNameBuff->AddOLE( mnLastRefIdx, nRes );        // nRes is storage ID
    }
    else
    {// dde link
        pExcRoot->pExtNameBuff->AddDDE( mnLastRefIdx );
    }
}

void ImportExcel::Colwidth()
{// Column Width
    sal_uInt8   nColFirst, nColLast;
    sal_uInt16  nColWidth;

    nColFirst = aIn.ReaduInt8();
    nColLast = aIn.ReaduInt8();
    nColWidth = aIn.ReaduInt16();

//TODO: add a check for the unlikely case of changed MAXCOL (-> XclImpAddressConverter)
//   if( nColLast > rD.MaxCol() )
//       nColLast = static_cast<sal_uInt16>(rD.MaxCol());

    sal_uInt16 nScWidth = XclTools::GetScColumnWidth( nColWidth, GetCharWidth() );
    pColRowBuff->SetWidthRange( nColFirst, nColLast, nScWidth );
}

void ImportExcel::Defrowheight2()
{
    sal_uInt16 nDefHeight;
    nDefHeight = maStrm.ReaduInt16();
    nDefHeight &= 0x7FFF;
    pColRowBuff->SetDefHeight( nDefHeight, EXC_DEFROW_UNSYNCED );
}

void ImportExcel::SheetProtect()
{
    if (GetRoot().GetBiff() != EXC_BIFF8)
        return;

    GetRoot().GetSheetProtectBuffer().ReadProtect( aIn, GetCurrScTab() );
}

void ImportExcel::DocProtect()
{
    if (GetRoot().GetBiff() != EXC_BIFF8)
        return;

    GetRoot().GetDocProtectBuffer().ReadDocProtect( aIn );
}

void ImportExcel::DocPassword()
{
    if (GetRoot().GetBiff() != EXC_BIFF8)
        return;

    GetRoot().GetDocProtectBuffer().ReadPasswordHash( aIn );
}

void ImportExcel::Codepage()
{
    SetCodePage( maStrm.ReaduInt16() );
}

void ImportExcel::Ixfe()
{
    mnIxfeIndex = maStrm.ReaduInt16();
}

void ImportExcel::DefColWidth()
{
    // stored as entire characters -> convert to 1/256 of characters (as in COLINFO)
    double fDefWidth = 256.0 * maStrm.ReaduInt16();

    if (!pColRowBuff)
    {
        SAL_WARN("sc", "*ImportExcel::DefColWidth(): pColRowBuff is NULL!");
        return;
    }

    // #i3006# additional space for default width - Excel adds space depending on font size
    tools::Long nFontHt = GetFontBuffer().GetAppFontData().mnHeight;
    fDefWidth += XclTools::GetXclDefColWidthCorrection( nFontHt );

    sal_uInt16 nScWidth = XclTools::GetScColumnWidth( limit_cast< sal_uInt16 >( fDefWidth ), GetCharWidth() );
    pColRowBuff->SetDefWidth( nScWidth );
}

void ImportExcel::Colinfo()
{// Column Formatting Information
    sal_uInt16  nColFirst, nColLast, nColWidth, nXF;
    sal_uInt16  nOpt;

    nColFirst = aIn.ReaduInt16();
    nColLast = aIn.ReaduInt16();
    nColWidth = aIn.ReaduInt16();
    nXF = aIn.ReaduInt16();
    nOpt = aIn.ReaduInt16();

    if( nColFirst > rD.MaxCol() )
        return;

    if( nColLast > rD.MaxCol() )
        nColLast = static_cast<sal_uInt16>(rD.MaxCol());

    bool bHidden = ::get_flag( nOpt, EXC_COLINFO_HIDDEN );
    bool bCollapsed = ::get_flag( nOpt, EXC_COLINFO_COLLAPSED );
    sal_uInt8 nLevel = ::extract_value< sal_uInt8 >( nOpt, 8, 3 );
    pColOutlineBuff->SetLevelRange( nColFirst, nColLast, nLevel, bCollapsed );

    if( bHidden )
        pColRowBuff->HideColRange( nColFirst, nColLast );

    sal_uInt16 nScWidth = XclTools::GetScColumnWidth( nColWidth, GetCharWidth() );
    pColRowBuff->SetWidthRange( nColFirst, nColLast, nScWidth );
    pColRowBuff->SetDefaultXF( nColFirst, nColLast, nXF );
}

void ImportExcel::Wsbool()
{
    sal_uInt16 nFlags;
    nFlags = aIn.ReaduInt16();

    pRowOutlineBuff->SetButtonMode( ::get_flag( nFlags, EXC_WSBOOL_ROWBELOW ) );
    pColOutlineBuff->SetButtonMode( ::get_flag( nFlags, EXC_WSBOOL_COLBELOW ) );

    GetPageSettings().SetFitToPages( ::get_flag( nFlags, EXC_WSBOOL_FITTOPAGE ) );
}

void ImportExcel::Boundsheet()
{
    sal_uInt16 nGrbit = 0;

    if( GetBiff() == EXC_BIFF5 )
    {
        aIn.DisableDecryption();
        maSheetOffsets.push_back( aIn.ReaduInt32() );
        aIn.EnableDecryption();
        nGrbit = aIn.ReaduInt16();
    }

    OUString aName( aIn.ReadByteString( false ) );

    SCTAB nScTab = nBdshtTab;
    if( nScTab > 0 )
    {
        OSL_ENSURE( !rD.HasTable( nScTab ), "ImportExcel::Boundsheet - sheet exists already" );
        rD.MakeTable( nScTab );
    }

    if( ( nGrbit & 0x0001 ) || ( nGrbit & 0x0002 ) )
        rD.SetVisible( nScTab, false );

    if( !rD.RenameTab( nScTab, aName ) )
    {
        rD.CreateValidTabName( aName );
        rD.RenameTab( nScTab, aName );
    }

    nBdshtTab++;
}

void ImportExcel::Country()
{
    sal_uInt16 nUICountry, nDocCountry;
    nUICountry = maStrm.ReaduInt16();
    nDocCountry = maStrm.ReaduInt16();

    // Store system language in XclRoot
    LanguageType eLanguage = ::msfilter::ConvertCountryToLanguage( static_cast< ::msfilter::CountryId >( nDocCountry ) );
    if( eLanguage != LANGUAGE_DONTKNOW )
        SetDocLanguage( eLanguage );

    // Set Excel UI language in add-in name translator
    eLanguage = ::msfilter::ConvertCountryToLanguage( static_cast< ::msfilter::CountryId >( nUICountry ) );
    if( eLanguage != LANGUAGE_DONTKNOW )
        SetUILanguage( eLanguage );
}

void ImportExcel::ReadUsesElfs()
{
    if( maStrm.ReaduInt16() != 0 )
    {
        ScDocOptions aDocOpt = GetDoc().GetDocOptions();
        aDocOpt.SetLookUpColRowNames( true );
        GetDoc().SetDocOptions( aDocOpt );
    }
}

void ImportExcel::Hideobj()
{
    sal_uInt16      nHide;
    ScVObjMode  eOle, eChart, eDraw;

    nHide = aIn.ReaduInt16();

    ScViewOptions aOpts( rD.GetViewOptions() );

    switch( nHide )
    {
        case 1:                         // Placeholders
            eOle   = VOBJ_MODE_SHOW;    // in Excel 97 only charts as place holder are displayed
            eChart = VOBJ_MODE_SHOW;    //#i80528# VOBJ_MODE_DUMMY replaced by VOBJ_MODE_SHOW now
            eDraw  = VOBJ_MODE_SHOW;
            break;
        case 2:                         // Hide all
            eOle   = VOBJ_MODE_HIDE;
            eChart = VOBJ_MODE_HIDE;
            eDraw  = VOBJ_MODE_HIDE;
            break;
        default:                        // Show all
            eOle   = VOBJ_MODE_SHOW;
            eChart = VOBJ_MODE_SHOW;
            eDraw  = VOBJ_MODE_SHOW;
            break;
    }

    aOpts.SetObjMode(sc::ViewObjectType::OLE,   eOle );
    aOpts.SetObjMode(sc::ViewObjectType::CHART, eChart );
    aOpts.SetObjMode(sc::ViewObjectType::DRAW,  eDraw );

    rD.SetViewOptions( aOpts );
}

void ImportExcel::Standardwidth()
{
    sal_uInt16 nScWidth = XclTools::GetScColumnWidth( maStrm.ReaduInt16(), GetCharWidth() );
    if (!pColRowBuff)
    {
        SAL_WARN("sc", "*ImportExcel::Standardwidth(): pColRowBuff is NULL!");
        return;
    }
    pColRowBuff->SetDefWidth( nScWidth, true );
}

void ImportExcel::Shrfmla()
{
    switch (mnLastRecId)
    {
        case EXC_ID2_FORMULA:
        case EXC_ID3_FORMULA:
        case EXC_ID4_FORMULA:
            // This record MUST immediately follow a FORMULA record.
        break;
        default:
            return;
    }

    if (!mpLastFormula)
        // The last FORMULA record should have left this data.
        return;

    aIn.Ignore( 8 );
    sal_uInt16 nLenExpr = aIn.ReaduInt16();

    // read mark is now on the formula

    std::unique_ptr<ScTokenArray> pResult;

    // The shared range in this record is erroneous more than half the time.
    // Don't ever rely on it. Use the one from the formula cell above.
    SCCOL nCol1 = mpLastFormula->mnCol;
    SCROW nRow1 = mpLastFormula->mnRow;

    ScAddress aPos(nCol1, nRow1, GetCurrScTab());
    pFormConv->Reset(aPos);
    pFormConv->Convert( pResult, maStrm, nLenExpr, true, FT_SharedFormula );

    if (!pResult)
    {
        SAL_WARN("sc", "+ImportExcel::Shrfmla(): ScTokenArray is NULL!");
        return;
    }

    pExcRoot->pShrfmlaBuff->Store(aPos, *pResult);

    // Create formula cell for the last formula record.

    ScDocumentImport& rDoc = GetDocImport();

    ScFormulaCell* pCell = new ScFormulaCell(rD, aPos, std::move(pResult));
    pCell->GetCode()->WrapReference(aPos, EXC_MAXCOL8, EXC_MAXROW8);
    rDoc.getDoc().CheckLinkFormulaNeedingCheck( *pCell->GetCode());
    rDoc.getDoc().EnsureTable(aPos.Tab());
    rDoc.setFormulaCell(aPos, pCell);
    pCell->SetNeedNumberFormat(false);
    if (std::isfinite(mpLastFormula->mfValue))
        pCell->SetResultDouble(mpLastFormula->mfValue);

    GetXFRangeBuffer().SetXF(aPos, mpLastFormula->mnXF);
    mpLastFormula->mpCell = pCell;
}

void ImportExcel::Mulrk()
{
    /* rw (2 bytes):  An Rw structure that specifies the row containing the
       cells with numeric data.

       colFirst (2 bytes):  A Col structure that specifies the first column in
       the series of numeric cells within the sheet. The value of colFirst.col
       MUST be less than or equal to 254.

       rgrkrec (variable): An array of RkRec structures.  Each element in the
       array specifies an RkRec in the row. The number of entries in the array
       MUST be equal to the value given by the following formula:

       Number of entries in rgrkrec = (colLast.col – colFirst.col +1)

       colLast (2 bytes):  A Col structure that specifies the last column in
       the set of numeric cells within the sheet. This colLast.col value MUST
       be greater than the colFirst.col value.  */

    XclAddress aXclPos;
    aIn >> aXclPos;

    XclAddress aCurrXclPos(aXclPos);
    while (true)
    {
        if (aXclPos.mnCol > aCurrXclPos.mnCol)
            break;
        if (aIn.GetRecLeft() <= 2)
            break;

        sal_uInt16 nXF = aIn.ReaduInt16();
        sal_Int32 nRkNum = aIn.ReadInt32();

        ScAddress aScPos( ScAddress::UNINITIALIZED );
        if( GetAddressConverter().ConvertAddress( aScPos, aCurrXclPos, GetCurrScTab(), true ) )
        {
            GetXFRangeBuffer().SetXF( aScPos, nXF );
            GetDocImport().setNumericCell(aScPos, XclTools::GetDoubleFromRK(nRkNum));
        }
        ++aCurrXclPos.mnCol;
    }
}

void ImportExcel::Mulblank()
{
    /* rw (2 bytes):  An Rw structure that specifies a row containing the blank
       cells.

       colFirst (2 bytes):  A Col structure that specifies the first column in
       the series of blank cells within the sheet. The value of colFirst.col
       MUST be less than or equal to 254.

       rgixfe (variable): An array of IXFCell structures. Each element of this
       array contains an IXFCell structure corresponding to a blank cell in the
       series. The number of entries in the array MUST be equal to the value
       given by the following formula:

       Number of entries in rgixfe = (colLast.col – colFirst.col +1)

       colLast (2 bytes):  A Col structure that specifies the last column in
       the series of blank cells within the sheet. This colLast.col value MUST
       be greater than colFirst.col value. */

    XclAddress aXclPos;
    aIn >> aXclPos;

    XclAddress aCurrXclPos(aXclPos);
    while (true)
    {
        if (aXclPos.mnCol > aCurrXclPos.mnCol)
            break;
        if (aIn.GetRecLeft() <= 2)
            break;

        sal_uInt16 nXF = aIn.ReaduInt16();

        ScAddress aScPos( ScAddress::UNINITIALIZED );
        if( GetAddressConverter().ConvertAddress( aScPos, aCurrXclPos, GetCurrScTab(), true ) )
            GetXFRangeBuffer().SetBlankXF( aScPos, nXF );
        ++aCurrXclPos.mnCol;
    }
}

void ImportExcel::Rstring()
{
    XclAddress aXclPos;
    sal_uInt16 nXFIdx;
    aIn >> aXclPos;
    nXFIdx = aIn.ReaduInt16();

    ScAddress aScPos( ScAddress::UNINITIALIZED );
    if( !GetAddressConverter().ConvertAddress( aScPos, aXclPos, GetCurrScTab(), true ) )
        return;

    // unformatted Unicode string with separate formatting information
    XclImpString aString;
    aString.Read( maStrm );

    // character formatting runs
    if( !aString.IsRich() )
        aString.ReadFormats( maStrm );

    GetXFRangeBuffer().SetXF( aScPos, nXFIdx );
    XclImpStringHelper::SetToDocument(GetDocImport(), aScPos, *this, aString, nXFIdx);
}

void ImportExcel::Cellmerging()
{
    XclImpAddressConverter& rAddrConv = GetAddressConverter();
    SCTAB nScTab = GetCurrScTab();

    sal_uInt16 nCount = maStrm.ReaduInt16();
    sal_uInt16 nIdx = 0;
    while (true)
    {
        if (maStrm.GetRecLeft() < 8)
            break;
        if (nIdx >= nCount)
            break;
        XclRange aXclRange;
        maStrm >> aXclRange;    // 16-bit rows and columns
        ScRange aScRange( ScAddress::UNINITIALIZED );
        if( rAddrConv.ConvertRange( aScRange, aXclRange, nScTab, nScTab, true ) )
        {
            const bool bTooSlowForFuzzing = mbFuzzing && (aScRange.aEnd.Col() > 512 || aScRange.aEnd.Row() > 512);
            if (!bTooSlowForFuzzing)
                GetXFRangeBuffer().SetMerge( aScRange.aStart.Col(), aScRange.aStart.Row(), aScRange.aEnd.Col(), aScRange.aEnd.Row() );
        }
        ++nIdx;
    }
}

void ImportExcel::Olesize()
{
    XclRange aXclOleSize( ScAddress::UNINITIALIZED );
    maStrm.Ignore( 2 );
    aXclOleSize.Read( maStrm, false );

    SCTAB nScTab = GetCurrScTab();
    GetAddressConverter().ConvertRange( maScOleSize, aXclOleSize, nScTab, nScTab, false );
}

void ImportExcel::Row34()
{
    sal_uInt16  nRow, nRowHeight, nGrbit, nXF;

    nRow = aIn.ReaduInt16();
    aIn.Ignore( 4 );

    SCROW nScRow = static_cast< SCROW >( nRow );

    if( !GetRoot().GetDoc().ValidRow( nScRow ) )
        return;

    nRowHeight = aIn.ReaduInt16();  // specify direct in Twips
    aIn.Ignore( 4 );

    nRowHeight = nRowHeight & 0x7FFF; // Bit 15: Row Height not changed manually
    if( !nRowHeight )
        nRowHeight = (GetBiff() == EXC_BIFF2) ? 0x25 : 0x225;

    nGrbit = aIn.ReaduInt16();
    nXF = aIn.ReaduInt16();

    sal_uInt8 nLevel = ::extract_value< sal_uInt8 >( nGrbit, 0, 3 );
    pRowOutlineBuff->SetLevel( nScRow, nLevel, ::get_flag( nGrbit, EXC_ROW_COLLAPSED ) );
    pColRowBuff->SetRowSettings( nScRow, nRowHeight, nGrbit );

    if( nGrbit & EXC_ROW_USEDEFXF )
        GetXFRangeBuffer().SetRowDefXF( nScRow, nXF & EXC_ROW_XFMASK );
}

void ImportExcel::Bof3()
{
    sal_uInt16 nSubType;
    maStrm.DisableDecryption();
    maStrm.Ignore( 2 );
    nSubType = maStrm.ReaduInt16();

    OSL_ENSURE( nSubType != 0x0100, "*ImportExcel::Bof3(): Biff3 as Workbook?!" );
    if( nSubType == 0x0100 )        // Book
        pExcRoot->eDateiTyp = Biff3W;
    else if( nSubType == 0x0020 )   // Chart
        pExcRoot->eDateiTyp = Biff3C;
    else if( nSubType == 0x0040 )   // Macro
        pExcRoot->eDateiTyp = Biff3M;
    else                            // #i51490# Excel interprets invalid indexes as worksheet
        pExcRoot->eDateiTyp = Biff3;
}

void ImportExcel::Array34()
{
    sal_uInt16                  nFirstRow, nLastRow, nFormLen;
    sal_uInt8                   nFirstCol, nLastCol;

    nFirstRow = aIn.ReaduInt16();
    nLastRow = aIn.ReaduInt16();
    nFirstCol = aIn.ReaduInt8();
    nLastCol = aIn.ReaduInt8();
    aIn.Ignore( (GetBiff() >= EXC_BIFF5) ? 6 : 2 );
    nFormLen = aIn.ReaduInt16();

    std::unique_ptr<ScTokenArray> pResult;

    if( GetRoot().GetDoc().ValidColRow( nLastCol, nLastRow ) )
    {
        // the read mark is now on the formula, length in nFormLen

        pFormConv->Reset( ScAddress( static_cast<SCCOL>(nFirstCol),
                    static_cast<SCROW>(nFirstRow), GetCurrScTab() ) );
        pFormConv->Convert( pResult, maStrm, nFormLen, true );

        SAL_WARN_IF(!pResult, "sc", "+ImportExcel::Array34(): ScTokenArray is NULL!");
    }

    if (pResult)
    {
        ScDocumentImport& rDoc = GetDocImport();
        ScRange aArrayRange(nFirstCol, nFirstRow, GetCurrScTab(), nLastCol, nLastRow, GetCurrScTab());
        rDoc.setMatrixCells(aArrayRange, *pResult, formula::FormulaGrammar::GRAM_ENGLISH_XL_A1);
    }
}

void ImportExcel::Defrowheight345()
{
    sal_uInt16 nFlags, nDefHeight;
    nFlags = maStrm.ReaduInt16();
    nDefHeight = maStrm.ReaduInt16();

    if (!pColRowBuff)
    {
        SAL_WARN("sc", "*ImportExcel::Defrowheight345(): pColRowBuff is NULL!");
        return;
    }

    pColRowBuff->SetDefHeight( nDefHeight, nFlags );
}

void ImportExcel::TableOp()
{
    sal_uInt16 nFirstRow = aIn.ReaduInt16();
    sal_uInt16 nLastRow = aIn.ReaduInt16();
    sal_uInt8 nFirstCol = aIn.ReaduInt8();
    sal_uInt8 nLastCol = aIn.ReaduInt8();
    sal_uInt16 nGrbit = aIn.ReaduInt16();
    sal_uInt16 nInpRow = aIn.ReaduInt16();
    sal_uInt16 nInpCol = aIn.ReaduInt16();
    sal_uInt16 nInpRow2 = aIn.ReaduInt16();
    sal_uInt16 nInpCol2 = aIn.ReaduInt16();

    if (mbFuzzing)
    {
        //shrink to smallish arbitrary value to not timeout
        nLastRow = std::min<sal_uInt16>(nLastRow, MAXROW_30 / 2);
    }

    if( GetRoot().GetDoc().ValidColRow( nLastCol, nLastRow ) )
    {
        if( nFirstCol && nFirstRow )
        {
            ScTabOpParam aTabOpParam;
            aTabOpParam.meMode = (nGrbit & EXC_TABLEOP_BOTH) ? ScTabOpParam::Both : ((nGrbit & EXC_TABLEOP_ROW) ? ScTabOpParam::Row : ScTabOpParam::Column);
            sal_uInt16 nCol = nFirstCol - 1;
            sal_uInt16 nRow = nFirstRow - 1;
            SCTAB nTab = GetCurrScTab();
            switch (aTabOpParam.meMode)
            {
                case ScTabOpParam::Column:
                    aTabOpParam.aRefFormulaCell.Set(
                            static_cast<SCCOL>(nFirstCol),
                            static_cast<SCROW>(nFirstRow - 1), nTab, false,
                            false, false );
                    aTabOpParam.aRefFormulaEnd.Set(
                            static_cast<SCCOL>(nLastCol),
                            static_cast<SCROW>(nFirstRow - 1), nTab, false,
                            false, false );
                    aTabOpParam.aRefColCell.Set( static_cast<SCCOL>(nInpCol),
                            static_cast<SCROW>(nInpRow), nTab, false, false,
                            false );
                    nRow++;
                break;
                case ScTabOpParam::Row:
                    aTabOpParam.aRefFormulaCell.Set(
                            static_cast<SCCOL>(nFirstCol - 1),
                            static_cast<SCROW>(nFirstRow), nTab, false, false,
                            false );
                    aTabOpParam.aRefFormulaEnd.Set(
                            static_cast<SCCOL>(nFirstCol - 1),
                            static_cast<SCROW>(nLastRow), nTab, false, false,
                            false );
                    aTabOpParam.aRefRowCell.Set( static_cast<SCCOL>(nInpCol),
                            static_cast<SCROW>(nInpRow), nTab, false, false,
                            false );
                    nCol++;
                break;
                case ScTabOpParam::Both:     // TWO-INPUT
                    aTabOpParam.aRefFormulaCell.Set(
                            static_cast<SCCOL>(nFirstCol - 1),
                            static_cast<SCROW>(nFirstRow - 1), nTab, false,
                            false, false );
                    aTabOpParam.aRefRowCell.Set( static_cast<SCCOL>(nInpCol),
                            static_cast<SCROW>(nInpRow), nTab, false, false,
                            false );
                    aTabOpParam.aRefColCell.Set( static_cast<SCCOL>(nInpCol2),
                            static_cast<SCROW>(nInpRow2), nTab, false, false,
                            false );
                break;
            }

            ScDocumentImport& rDoc = GetDocImport();
            ScRange aTabOpRange(nCol, nRow, nTab, nLastCol, nLastRow, nTab);
            rDoc.setTableOpCells(aTabOpRange, aTabOpParam);
        }
    }
    else
    {
        bTabTruncated = true;
        GetTracer().TraceInvalidRow(nLastRow, rD.MaxRow());
    }
}

void ImportExcel::Bof4()
{
    sal_uInt16 nSubType;
    maStrm.DisableDecryption();
    maStrm.Ignore( 2 );
    nSubType = maStrm.ReaduInt16();

    if( nSubType == 0x0100 )        // Book
        pExcRoot->eDateiTyp = Biff4W;
    else if( nSubType == 0x0020 )   // Chart
        pExcRoot->eDateiTyp = Biff4C;
    else if( nSubType == 0x0040 )   // Macro
        pExcRoot->eDateiTyp = Biff4M;
    else                            // #i51490# Excel interprets invalid indexes as worksheet
        pExcRoot->eDateiTyp = Biff4;
}

void ImportExcel::Bof5()
{
    //POST: eDateiTyp = Type of the file to be read
    sal_uInt16      nSubType, nVers;
    BiffTyp     eDatei;

    maStrm.DisableDecryption();
    nVers = maStrm.ReaduInt16();
    nSubType = maStrm.ReaduInt16(  );

    switch( nSubType )
    {
        case 0x0005:    eDatei = Biff5W;    break;  // workbook globals
        case 0x0006:    eDatei = Biff5V;    break;  // VB module
        case 0x0020:    eDatei = Biff5C;    break;  // chart
        case 0x0040:    eDatei = Biff5M4;   break;  // macro sheet
        case 0x0010:                                // worksheet
        default:        eDatei = Biff5;     break;  // tdf#144732 Excel interprets invalid indexes as worksheet
    }

    if( nVers == 0x0600 && (GetBiff() == EXC_BIFF8) )
        eDatei = static_cast<BiffTyp>( eDatei - Biff5 + Biff8 );

    pExcRoot->eDateiTyp = eDatei;
}

void ImportExcel::EndSheet()
{
    pExcRoot->pExtSheetBuff->Reset();

    if( GetBiff() <= EXC_BIFF5 )
    {
        pExcRoot->pExtNameBuff->Reset();
        mnLastRefIdx = 0;
    }

    FinalizeTable();
}

void ImportExcel::NewTable()
{
    SCTAB nTab = GetCurrScTab();
    if( nTab > 0 && !rD.HasTable( nTab ) )
        rD.MakeTable( nTab );

    if (nTab == 0 && GetBiff() == EXC_BIFF2)
    {
        // For Excel 2.1 Worksheet file, we need to set the file name as the
        // sheet name.
        INetURLObject aURL(GetDocUrl());
        rD.RenameTab(0, aURL.getBase());
    }

    pExcRoot->pShrfmlaBuff->Clear();
    maLastFormulaCells.clear();
    mpLastFormula = nullptr;

    InitializeTable( nTab );

    XclImpOutlineDataBuffer* pNewItem = new XclImpOutlineDataBuffer( GetRoot(), nTab );
    pOutlineListBuffer->push_back( std::unique_ptr<XclImpOutlineDataBuffer>(pNewItem) );
    pExcRoot->pColRowBuff = pColRowBuff = pNewItem->GetColRowBuff();
    pColOutlineBuff = pNewItem->GetColOutline();
    pRowOutlineBuff = pNewItem->GetRowOutline();
}

std::unique_ptr<ScTokenArray> ImportExcel::ErrorToFormula( bool bErrOrVal, sal_uInt8 nError, double& rVal )
{
    return pFormConv->GetBoolErr( XclTools::ErrorToEnum( rVal, bErrOrVal, nError ) );
}

void ImportExcel::AdjustRowHeight()
{
    /*  Speed up chart import: import all sheets without charts, then
        update row heights (here), last load all charts -> do not any longer
        update inside of ScDocShell::ConvertFrom() (causes update of existing
        charts during each and every change of row height). */
    if( ScModelObj* pDocObj = GetDocModelObj() )
        pDocObj->UpdateAllRowHeights();
}

void ImportExcel::PostDocLoad()
{
    /*  Set automatic page numbering in Default page style (default is "page number = 1").
        Otherwise hidden tables (i.e. for scenarios) which have Default page style will
        break automatic page numbering. */
    if( SfxStyleSheetBase* pStyleSheet = GetStyleSheetPool().Find( ScResId( STR_STYLENAME_STANDARD ), SfxStyleFamily::Page ) )
        pStyleSheet->GetItemSet().Put( SfxUInt16Item( ATTR_PAGE_FIRSTPAGENO, 0 ) );

    // outlines for all sheets, sets hidden rows and columns (#i11776# after filtered ranges)
    for (auto& rxBuffer : *pOutlineListBuffer)
        rxBuffer->Convert();

    // document view settings (before visible OLE area)
    GetDocViewSettings().Finalize();

    // process all drawing objects (including OLE, charts, controls; after hiding rows/columns; before visible OLE area)
    GetObjectManager().ConvertObjects();

    // visible area (used if this document is an embedded OLE object)
    if( ScDocShell* pDocShell = GetDocShell() )
    {
        // visible area if embedded
        const ScExtDocSettings& rDocSett = GetExtDocOptions().GetDocSettings();
        SCTAB nDisplScTab = rDocSett.mnDisplTab;

        /*  #i44077# If a new OLE object is inserted from file, there is no
            OLESIZE record in the Excel file. Calculate used area from file
            contents (used cells and drawing objects). */
        if( !maScOleSize.IsValid() )
        {
            // used area of displayed sheet (cell contents)
            if( const ScExtTabSettings* pTabSett = GetExtDocOptions().GetTabSettings( nDisplScTab ) )
                maScOleSize = pTabSett->maUsedArea;
            // add all valid drawing objects
            ScRange aScObjArea = GetObjectManager().GetUsedArea( nDisplScTab );
            if( aScObjArea.IsValid() )
                maScOleSize.ExtendTo( aScObjArea );
        }

        // valid size found - set it at the document
        if( maScOleSize.IsValid() )
        {
            pDocShell->SetVisArea( GetDoc().GetMMRect(
                maScOleSize.aStart.Col(), maScOleSize.aStart.Row(),
                maScOleSize.aEnd.Col(), maScOleSize.aEnd.Row(), nDisplScTab ) );
            GetDoc().SetVisibleTab( nDisplScTab );
        }
    }

    // open forms in alive mode (has no effect, if no controls in document)
    if( ScModelObj* pDocObj = GetDocModelObj() )
        pDocObj->setPropertyValue( SC_UNO_APPLYFMDES, uno::Any( false ) );

    // enables extended options to be set to the view after import
    GetExtDocOptions().SetChanged( true );

    // root data owns the extended document options -> create a new object
    GetDoc().SetExtDocOptions( std::make_unique<ScExtDocOptions>( GetExtDocOptions() ) );

    const SCTAB     nLast = rD.GetTableCount();
    const ScRange*      p;

    if( GetRoot().GetPrintAreaBuffer().HasRanges() )
    {
        for( SCTAB n = 0 ; n < nLast ; n++ )
        {
            p = GetRoot().GetPrintAreaBuffer().First(n);
            if( p )
            {
                rD.ClearPrintRanges( n );
                while( p )
                {
                    rD.AddPrintRange( n, *p );
                    p = GetRoot().GetPrintAreaBuffer().Next();
                }
            }
            else
            {
                // #i4063# no print ranges -> print entire sheet
                rD.SetPrintEntireSheet( n );
            }
        }
        GetTracer().TracePrintRange();
    }

    if( !GetRoot().GetTitleAreaBuffer().HasRanges() )
        return;

    for( SCTAB n = 0 ; n < nLast ; n++ )
    {
        p = GetRoot().GetTitleAreaBuffer().First(n);
        if( p )
        {
            bool    bRowVirgin = true;
            bool    bColVirgin = true;

            while( p )
            {
                if( p->aStart.Col() == 0 && p->aEnd.Col() == rD.MaxCol() && bRowVirgin )
                {
                    rD.SetRepeatRowRange( n, *p );
                    bRowVirgin = false;
                }

                if( p->aStart.Row() == 0 && p->aEnd.Row() == rD.MaxRow() && bColVirgin )
                {
                    rD.SetRepeatColRange( n, *p );
                    bColVirgin = false;
                }

                p = GetRoot().GetTitleAreaBuffer().Next();
            }
        }
    }
}

XclImpOutlineDataBuffer::XclImpOutlineDataBuffer( const XclImpRoot& rRoot, SCTAB nScTab ) :
    XclImpRoot( rRoot ),
    mxColOutlineBuff( std::make_shared<XclImpOutlineBuffer>( rRoot.GetXclMaxPos().Col() + 1 ) ),
    mxRowOutlineBuff( std::make_shared<XclImpOutlineBuffer>( rRoot.GetXclMaxPos().Row() + 1 ) ),
    mxColRowBuff( std::make_shared<XclImpColRowSettings>( rRoot ) ),
    mnScTab( nScTab )
{
}

XclImpOutlineDataBuffer::~XclImpOutlineDataBuffer()
{
}

void XclImpOutlineDataBuffer::Convert()
{
    mxColOutlineBuff->SetOutlineArray( &GetDoc().GetOutlineTable( mnScTab, true )->GetColArray() );
    mxColOutlineBuff->MakeScOutline();

    mxRowOutlineBuff->SetOutlineArray( &GetDoc().GetOutlineTable( mnScTab, true )->GetRowArray() );
    mxRowOutlineBuff->MakeScOutline();

    mxColRowBuff->ConvertHiddenFlags( mnScTab );
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
