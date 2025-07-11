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


#include <cstdio>

#include <comphelper/lok.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <com/sun/star/drawing/XDrawPage.hpp>
#include <com/sun/star/drawing/XDrawPagesSupplier.hpp>
#include <com/sun/star/drawing/XDrawView.hpp>
#include <com/sun/star/drawing/XMasterPagesSupplier.hpp>
#include <com/sun/star/drawing/XMasterPageTarget.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XController.hpp>
#include <com/sun/star/io/IOException.hpp>
#include <com/sun/star/view/XSelectionSupplier.hpp>
#include <com/sun/star/drawing/XSlideSorterSelectionSupplier.hpp>

#include <unotools/mediadescriptor.hxx>
#include <unotools/ucbstreamhelper.hxx>
#include <tools/debug.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <tools/zcodec.hxx>

#include <drawinglayer/primitive2d/baseprimitive2d.hxx>
#include <drawinglayer/primitive2d/Primitive2DContainer.hxx>
#include <drawinglayer/primitive2d/drawinglayer_primitivetypes2d.hxx>

#include "svgfilter.hxx"

#include <svx/unopage.hxx>
#include <vcl/graphicfilter.hxx>
#include <vcl/window.hxx>
#include <svx/svdmodel.hxx>
#include <svx/svdpage.hxx>
#include <svx/svdograf.hxx>
#include <svl/itempool.hxx>

#include <memory>

using namespace ::com::sun::star;

namespace
{
    constexpr OUStringLiteral constFilterNameDraw = u"svg_Scalable_Vector_Graphics_Draw";
    constexpr OUStringLiteral constFilterName = u"svg_Scalable_Vector_Graphics";
}

SVGFilter::SVGFilter( const Reference< XComponentContext >& rxCtx ) :
    mxContext( rxCtx ),
    mpSVGDoc( nullptr ),
    mpSVGFontExport( nullptr ),
    mpSVGWriter( nullptr ),
    mbSinglePage( false ),
    mnVisiblePage( -1 ),
    mpObjects( nullptr ),
    mbExportShapeSelection(false),
    mbIsPreview(false),
    mbShouldCompress(false),
    mbWriterFilter(false),
    mbCalcFilter(false),
    mbImpressFilter(false),
    mpDefaultSdrPage( nullptr ),
    mbPresentation( false )
{
}

SVGFilter::~SVGFilter()
{
    DBG_ASSERT( mpSVGDoc == nullptr, "mpSVGDoc not destroyed" );
    DBG_ASSERT( mpSVGExport == nullptr, "mpSVGExport not destroyed" );
    DBG_ASSERT( mpSVGFontExport == nullptr, "mpSVGFontExport not destroyed" );
    DBG_ASSERT( mpSVGWriter == nullptr, "mpSVGWriter not destroyed" );
    DBG_ASSERT( mpObjects == nullptr, "mpObjects not destroyed" );
}

sal_Bool SAL_CALL SVGFilter::filter( const Sequence< PropertyValue >& rDescriptor )
{
    mbWriterFilter = false;
    mbCalcFilter = false;
    mbImpressFilter = false;
    mbShouldCompress = false;

    if(mxDstDoc.is()) // Import works for Impress / draw only
        return filterImpressOrDraw(rDescriptor);

    if(!mxSrcDoc)
        return false;

    for (const PropertyValue& rProp : rDescriptor)
    {
        if (rProp.Name == "IsPreview")
        {
            rProp.Value >>= mbIsPreview;
            break;
        }
    }

    for (const PropertyValue& rProp : rDescriptor)
    {
        if (rProp.Name == "FilterName")
        {
            OUString sFilterName;
            rProp.Value >>= sFilterName;
            if(sFilterName == "impress_svg_Export")
            {
                mbImpressFilter = true;
                return filterImpressOrDraw(rDescriptor);
            }
            else if(sFilterName == "writer_svg_Export")
            {
                mbWriterFilter = true;
                return filterWriterOrCalc(rDescriptor);
            }
            else if(sFilterName == "calc_svg_Export")
            {
                mbCalcFilter = true;
                return filterWriterOrCalc(rDescriptor);
            }
            else if(sFilterName == "draw_svgz_Export")
            {
                mbShouldCompress = true;
            }
            break;
        }
    }
    return filterImpressOrDraw(rDescriptor);
}

css::uno::Reference<css::frame::XController> SVGFilter::getSourceController() const
{
    uno::Reference<frame::XController> xController;
    // Current frame may be e.g. Basic. Try to get a controller from the source model first.
    if (auto xModel = mxSrcDoc.query<frame::XModel>())
        xController = xModel->getCurrentController();
    // Try current frame as a fallback.
    if (!xController)
    {
        uno::Reference<frame::XDesktop2> xDesktop(frame::Desktop::create(mxContext));
        if (auto xFrame = xDesktop->getCurrentFrame()) // Manage headless case
            xController = xFrame->getController();
    }
    return xController;
}

css::uno::Reference<css::frame::XController> SVGFilter::fillDrawImpressSelectedPages()
{
    uno::Reference<frame::XController> xController = getSourceController();

    uno::Reference<drawing::XSlideSorterSelectionSupplier> xSlideSorterSelection(xController, uno::UNO_QUERY);
    if (xSlideSorterSelection)
    {
        Sequence<Reference<XInterface>> aSelectedPageSequence;
        if (xSlideSorterSelection->getSlideSorterSelection() >>= aSelectedPageSequence)
        {
            for (auto& xInterface : aSelectedPageSequence)
            {
                uno::Reference<drawing::XDrawPage> xDrawPage(xInterface, uno::UNO_QUERY);

                if (Reference<XPropertySet> xPropSet{ xDrawPage, UNO_QUERY })
                {
                    Reference<XPropertySetInfo> xPropSetInfo = xPropSet->getPropertySetInfo();
                    if (xPropSetInfo && xPropSetInfo->hasPropertyByName(u"Visible"_ustr))
                    {
                        bool bIsSlideVisible = true; // default: true
                        xPropSet->getPropertyValue(u"Visible"_ustr) >>= bIsSlideVisible;
                        if (!bIsSlideVisible)
                            continue;
                    }
                }
                mSelectedPages.push_back(xDrawPage);
            }
            if (!mSelectedPages.empty())
                return xController;
        }
    }

    if (mSelectedPages.empty())
    {
        // apparently failed to get a selection - fallback to current page
        if (auto xDrawView = xController.query<drawing::XDrawView>())
            mSelectedPages.push_back(xDrawView->getCurrentPage());
    }
    return xController;
}

bool SVGFilter::filterImpressOrDraw( const Sequence< PropertyValue >& rDescriptor )
{
    SolarMutexGuard aGuard;
    vcl::Window* pFocusWindow(Application::GetFocusWindow());
    bool bRet(false);

    if(pFocusWindow)
    {
        pFocusWindow->EnterWait();
    }

    if(mxDstDoc.is())
    {
        // Import. Use an endless loop to have easy exits for error handling
        while(true)
        {
            // use MediaDescriptor to get needed data out of Sequence< PropertyValue >
            utl::MediaDescriptor aMediaDescriptor(rDescriptor);
            uno::Reference<io::XInputStream> xInputStream;

            xInputStream.set(aMediaDescriptor[utl::MediaDescriptor::PROP_INPUTSTREAM], UNO_QUERY);

            if(!xInputStream.is())
            {
                // we need the InputStream
                break;
            }

            // get the DrawPagesSupplier
            uno::Reference< drawing::XDrawPagesSupplier > xDrawPagesSupplier( mxDstDoc, uno::UNO_QUERY );

            if(!xDrawPagesSupplier.is())
            {
                // we need the DrawPagesSupplier
                break;
            }

            // get the DrawPages
            uno::Reference< drawing::XDrawPages > xDrawPages = xDrawPagesSupplier->getDrawPages();

            if(!xDrawPages.is())
            {
                // we need the DrawPages
                break;
            }

            // check DrawPageCount (there should be one by default)
            sal_Int32 nDrawPageCount(xDrawPages->getCount());

            if(0 == nDrawPageCount)
            {
                // at least one DrawPage should be there - we need that
                break;
            }

            // get that DrawPage
            uno::Reference< drawing::XDrawPage > xDrawPage( xDrawPages->getByIndex(0), uno::UNO_QUERY );

            if(!xDrawPage.is())
            {
                // we need that DrawPage
                break;
            }

            // get that DrawPage's UNO API implementation
            SvxDrawPage* pSvxDrawPage(comphelper::getFromUnoTunnel<SvxDrawPage>(xDrawPage));

            if(nullptr == pSvxDrawPage || nullptr == pSvxDrawPage->GetSdrPage())
            {
                // we need a SvxDrawPage
                break;
            }

            // get the SvStream to work with
            std::unique_ptr< SvStream > aStream(utl::UcbStreamHelper::CreateStream(xInputStream, true));

            if (!aStream)
            {
                // we need the SvStream
                break;
            }

            // create a GraphicFilter and load the SVG (format already known, thus *could*
            // be handed over to ImportGraphic - but detection is fast).
            // As a bonus, zipped data is already detected and handled there
            GraphicFilter aGraphicFilter;
            Graphic aGraphic;
            const ErrCode nGraphicFilterErrorCode(
                aGraphicFilter.ImportGraphic(aGraphic, u"", *aStream));

            if(ERRCODE_NONE != nGraphicFilterErrorCode)
            {
                // SVG import error, cannot continue
                break;
            }

            // get the GraphicPrefSize early to check if we have any content
            // (the SVG may contain nothing and/or just <g visibility="hidden"> stuff...)
            const Size aGraphicPrefSize(aGraphic.GetPrefSize());

            if(0 == aGraphicPrefSize.Width() || 0 == aGraphicPrefSize.Height())
            {
                // SVG has no displayable content, stop import.
                // Also possible would be to get the sequence< Primitives >
                // from aGraphic and check if it is empty.
                // Possibility to set some error message here to tell
                // the user what/why loading went wrong, but I do not
                // know how this could be done here
                break;
            }

            // tdf#118232 Get the sequence of primitives and check if geometry is completely
            // hidden. If so, there is no need to add a SdrObject at all
            auto const & rVectorGraphicData(aGraphic.getVectorGraphicData());
            bool bContainsNoGeometry(false);

            if(bool(rVectorGraphicData) && VectorGraphicDataType::Svg == rVectorGraphicData->getType())
            {
                const drawinglayer::primitive2d::Primitive2DContainer aContainer(rVectorGraphicData->getPrimitive2DSequence());

                if(!aContainer.empty())
                {
                    bool bAllAreHiddenGeometry(true);

                    for(const auto& rCandidate : aContainer)
                    {
                        if(rCandidate && PRIMITIVE2D_ID_HIDDENGEOMETRYPRIMITIVE2D != rCandidate->getPrimitive2DID())
                        {
                            bAllAreHiddenGeometry = false;
                            break;
                        }
                    }

                    if(bAllAreHiddenGeometry)
                    {
                        bContainsNoGeometry = true;
                    }
                }
            }

            // create a SdrModel-GraphicObject to insert to page
            SdrPage* pTargetSdrPage(pSvxDrawPage->GetSdrPage());
            rtl::Reference< SdrGrafObj > aNewSdrGrafObj;

            // tdf#118232 only add an SdrGrafObj when we have Geometry
            if(!bContainsNoGeometry)
            {
                aNewSdrGrafObj =
                    new SdrGrafObj(
                        pTargetSdrPage->getSdrModelFromSdrPage(),
                        aGraphic);
            }

            // Evtl. adapt the GraphicPrefSize to target-MapMode of target-Model
            // (should be 100thmm here, but just stay safe by doing the conversion)
            const MapMode aGraphicPrefMapMode(aGraphic.GetPrefMapMode());
            const MapUnit eDestUnit(pTargetSdrPage->getSdrModelFromSdrPage().GetItemPool().GetMetric(0));
            const MapUnit eSrcUnit(aGraphicPrefMapMode.GetMapUnit());
            Size aGraphicSize(aGraphicPrefSize);

            if (eDestUnit != eSrcUnit)
            {
                aGraphicSize = Size(
                    OutputDevice::LogicToLogic(aGraphicSize.Width(), eSrcUnit, eDestUnit),
                    OutputDevice::LogicToLogic(aGraphicSize.Height(), eSrcUnit, eDestUnit));
            }

            // Based on GraphicSize, set size of Page. Do not forget to adapt PageBorders,
            // but interpret them relative to PageSize so that they do not 'explode/shrink'
            // in comparison. Use a common scaling factor for hor/ver to not get
            // asynchronous border distances, though. All in all this will adapt borders
            // nicely and is based on office-defaults for standard-page-border-sizes.
            const Size aPageSize(pTargetSdrPage->GetSize());
            const double fBorderRelation((
                static_cast< double >(pTargetSdrPage->GetLeftBorder()) / aPageSize.Width() +
                static_cast< double >(pTargetSdrPage->GetRightBorder()) / aPageSize.Width() +
                static_cast< double >(pTargetSdrPage->GetUpperBorder()) / aPageSize.Height() +
                static_cast< double >(pTargetSdrPage->GetLowerBorder()) / aPageSize.Height()) / 4.0);
            const tools::Long nAllBorder(basegfx::fround<tools::Long>((aGraphicSize.Width() + aGraphicSize.Height()) * fBorderRelation * 0.5));

            // Adapt PageSize and Border stuff. To get all MasterPages and PresObjs
            // correctly adapted, do not just use
            //      pTargetSdrPage->SetBorder(...) and
            //      pTargetSdrPage->SetSize(...),
            // but ::adaptSizeAndBorderForAllPages
            // Do use original Size and borders to get as close to original
            // as possible for better turn-arounds.
            pTargetSdrPage->getSdrModelFromSdrPage().adaptSizeAndBorderForAllPages(
                Size(
                    aGraphicSize.Width(),
                    aGraphicSize.Height()),
                nAllBorder,
                nAllBorder,
                nAllBorder,
                nAllBorder);

            // tdf#118232 set pos/size at SdrGraphicObj - use zero position for
            // better turn-around results
            if(!bContainsNoGeometry)
            {
                aNewSdrGrafObj->SetSnapRect(
                    tools::Rectangle(
                        Point(0, 0),
                        aGraphicSize));

                // insert to page (owner change of SdrGrafObj)
                pTargetSdrPage->InsertObject(aNewSdrGrafObj.get());
            }

            // done - set positive result now
            bRet = true;

            // always leave helper endless loop
            break;
        }
    }
    else if( mxSrcDoc.is() )
    {
        // #i124608# detect selection
        bool bSelectionOnly = false;
        bool bGotSelection = false;

        // when using LibreOfficeKit, default to exporting everything (-1)
        bool bPageProvided = comphelper::LibreOfficeKit::isActive();
        sal_Int32 nPageToExport = -1;

        for( const PropertyValue& rProp : rDescriptor )
        {
            if (rProp.Name == "SelectionOnly")
            {
                // #i124608# extract single selection wanted from dialog return values
                rProp.Value >>= bSelectionOnly;
                bPageProvided = false;
            }
            else if (rProp.Name == "PagePos")
            {
                rProp.Value >>= nPageToExport;
                bPageProvided = true;
            }
        }

        uno::Reference<frame::XController > xController;
        if (!bPageProvided)
            xController = fillDrawImpressSelectedPages();

        /*
         * Export all slides, or requested "PagePos"
         */
        if( mSelectedPages.empty() )
        {
            uno::Reference< drawing::XMasterPagesSupplier > xMasterPagesSupplier( mxSrcDoc, uno::UNO_QUERY );
            uno::Reference< drawing::XDrawPagesSupplier >   xDrawPagesSupplier( mxSrcDoc, uno::UNO_QUERY );

            if( xMasterPagesSupplier.is() && xDrawPagesSupplier.is() )
            {
                uno::Reference< drawing::XDrawPages >   xMasterPages = xMasterPagesSupplier->getMasterPages();
                uno::Reference< drawing::XDrawPages >   xDrawPages = xDrawPagesSupplier->getDrawPages();
                if( xMasterPages.is() && xMasterPages->getCount() &&
                    xDrawPages.is() && xDrawPages->getCount() )
                {
                    sal_Int32 nDPCount = xDrawPages->getCount();

                    sal_Int32 i;
                    for( i = 0; i < nDPCount; ++i )
                    {
                        if( nPageToExport != -1 && nPageToExport == i )
                        {
                            uno::Reference< drawing::XDrawPage > xDrawPage( xDrawPages->getByIndex( i ), uno::UNO_QUERY );
                            mSelectedPages.push_back(xDrawPage);
                        }
                        else
                        {
                            uno::Reference< drawing::XDrawPage > xDrawPage( xDrawPages->getByIndex( i ), uno::UNO_QUERY );
                            Reference< XPropertySet > xPropSet( xDrawPage, UNO_QUERY );
                            bool bIsSlideVisible = true;     // default: true
                            if (xPropSet.is())
                            {
                                Reference< XPropertySetInfo > xPropSetInfo = xPropSet->getPropertySetInfo();
                                if (xPropSetInfo.is() && xPropSetInfo->hasPropertyByName(u"Visible"_ustr))
                                {
                                    xPropSet->getPropertyValue( u"Visible"_ustr )  >>= bIsSlideVisible;
                                    if (!bIsSlideVisible)
                                        continue;
                                }
                            }
                            mSelectedPages.push_back(xDrawPage);
                        }
                    }
                }
            }
        }

        if (bSelectionOnly)
        {
            // #i124608# when selection only is wanted, get the current object selection
            // from the DrawView
            Reference< view::XSelectionSupplier > xSelection (xController, UNO_QUERY);

            if (xSelection.is())
            {
                bGotSelection
                    = ( xSelection->getSelection() >>= maShapeSelection );
            }
        }

        if(bSelectionOnly && bGotSelection && 0 == maShapeSelection->getCount())
        {
            // #i124608# export selection, got maShapeSelection but no shape selected -> nothing
            // to export, we are done (maybe return true, but a hint that nothing was done
            // may be useful; it may have happened by error)
            bRet = false;
        }
        else
        {
            /*
             *  We get all master page that are targeted by at least one draw page.
             *  The master page are put in an unordered set.
             */
            ObjectSet aMasterPageTargetSet;
            for(const uno::Reference<drawing::XDrawPage> & mSelectedPage : mSelectedPages)
            {
                uno::Reference< drawing::XMasterPageTarget > xMasterPageTarget( mSelectedPage, uno::UNO_QUERY );
                if( xMasterPageTarget.is() )
                {
                    aMasterPageTargetSet.insert( xMasterPageTarget->getMasterPage() );
                }
            }
            // Later we move them to an uno::Sequence so we can get them by index
            mMasterPageTargets.resize( aMasterPageTargetSet.size() );
            sal_Int32 i = 0;
            for (auto const& masterPageTarget : aMasterPageTargetSet)
            {
                mMasterPageTargets[i++].set(masterPageTarget,  uno::UNO_QUERY);
            }

            bRet = implExport( rDescriptor );
        }
    }
    else
        bRet = false;

    if( pFocusWindow )
        pFocusWindow->LeaveWait();

    return bRet;
}

bool SVGFilter::filterWriterOrCalc( const Sequence< PropertyValue >& rDescriptor )
{
    bool bSelectionOnly = false;

    for (const PropertyValue& rProp : rDescriptor)
    {
        if (rProp.Name == "SelectionOnly")
        {
            rProp.Value >>= bSelectionOnly;
            break;
        }
    }

    if(!bSelectionOnly) // For Writer only the selection-only mode is supported
        return false;

    Reference<view::XSelectionSupplier> xSelection(getSourceController(), UNO_QUERY);
    if (!xSelection.is())
        return false;

    // Select only one draw page
    uno::Reference< drawing::XDrawPagesSupplier > xDrawPagesSupplier( mxSrcDoc, uno::UNO_QUERY );
    uno::Reference<drawing::XDrawPages> xDrawPages = xDrawPagesSupplier->getDrawPages();
    mSelectedPages.resize( 1 );
    mSelectedPages[0].set(xDrawPages->getByIndex(0), uno::UNO_QUERY);

    bool bGotSelection = xSelection->getSelection() >>= maShapeSelection;

    if (!bGotSelection)
    {
        if (mbWriterFilter)
        {
            // For Writer we might have a non-shape graphic
            bGotSelection = implExportWriterTextGraphic(xSelection);
        }
        if (!bGotSelection)
            return false;
    }

    return implExport( rDescriptor );
}

void SAL_CALL SVGFilter::cancel( )
{
}

void SAL_CALL SVGFilter::setSourceDocument( const Reference< XComponent >& xDoc )
{
    mxSrcDoc = xDoc;
}

void SAL_CALL SVGFilter::setTargetDocument( const Reference< XComponent >& xDoc )
{
    mxDstDoc = xDoc;
}

namespace {

// There is already another SVG-Type_Detector, see
// vcl/source/filter/graphicfilter.cxx ("DOCTYPE svg"),
// but since these start from different preconditions it is not
// easy to unify these. For now, use this local helper.
class SVGFileInfo
{
private:
    const uno::Reference<io::XInputStream>&     mxInput;
    uno::Sequence< sal_Int8 >                   mnFirstBytes;
    sal_Int32                                   mnFirstBytesSize;
    sal_Int32                                   mnFirstRead;
    bool                                        mbProcessed;
    bool                                        mbIsSVG;

    bool impCheckForMagic(
        const sal_Int8* pMagic,
        const sal_Int32 nMagicSize)
    {
        const sal_Int8* pBuffer(mnFirstBytes.getConstArray());
        return std::search(
            pBuffer,
            pBuffer + mnFirstRead,
            pMagic,
            pMagic + nMagicSize) != pBuffer + mnFirstRead;
    }

    void impEnsureProcessed()
    {
        if(mbProcessed)
        {
            return;
        }

        mbProcessed = true;

        if(!mxInput.is())
        {
            return;
        }

        if(0 == mnFirstBytesSize)
        {
            return;
        }

        mnFirstBytes.realloc(mnFirstBytesSize);

        if(mnFirstBytesSize != mnFirstBytes.getLength())
        {
            return;
        }

        std::unique_ptr< SvStream > aStream(utl::UcbStreamHelper::CreateStream(mxInput, true));

        if (!aStream)
        {
            return;
        }

        const sal_uInt64 nStreamLen(aStream->remainingSize());

        if(aStream->GetError())
        {
            return;
        }

        mnFirstRead = aStream->ReadBytes(
            &mnFirstBytes.getArray()[0],
            std::min(nStreamLen, static_cast<sal_uInt64>(mnFirstBytesSize)));

        if(aStream->GetError())
        {
            return;
        }

        // check if it is gzipped -> svgz
        if (mnFirstBytes[0] == 0x1F && static_cast<sal_uInt8>(mnFirstBytes[1]) == 0x8B)
        {
            ZCodec aCodec;

            aCodec.BeginCompression(ZCODEC_DEFAULT_COMPRESSION, /*gzLib*/true);
            mnFirstRead = aCodec.Read(
                *aStream,
                reinterpret_cast< sal_uInt8* >(mnFirstBytes.getArray()),
                mnFirstBytesSize);
            aCodec.EndCompression();

            if (mnFirstRead < 0)
                return;
        }

        if(!mbIsSVG)
        {
            const sal_Int8 aMagic[] = {'<', 's', 'v', 'g'};
            const sal_Int32 nMagicSize(std::size(aMagic));

            mbIsSVG = impCheckForMagic(aMagic, nMagicSize);
        }

        if(!mbIsSVG)
        {
            const sal_Int8 aMagic[] = {'D', 'O', 'C', 'T', 'Y', 'P', 'E', ' ', 's', 'v', 'g'};
            const sal_Int32 nMagicSize(std::size(aMagic));

            mbIsSVG = impCheckForMagic(aMagic, nMagicSize);
        }

        return;
    }

public:
    SVGFileInfo(
        const uno::Reference<io::XInputStream>& xInput)
    :   mxInput(xInput),
        mnFirstBytesSize(2048),
        mnFirstRead(0),
        mbProcessed(false),
        mbIsSVG(false)
    {
        // For the default buffer size: Use not too big
        // (not more than 16K), but also not too small
        // (not less than 1/2K), see comments at
        // ImpPeekGraphicFormat, SVG section.
        // I remember these cases and it *can* happen
        // that SVGs have quite massive comments in their
        // headings (!)
        // Limit to plausible sizes, also for security reasons
        mnFirstBytesSize = std::min(sal_Int32(512), mnFirstBytesSize);
        mnFirstBytesSize = std::max(sal_Int32(16384), mnFirstBytesSize);
    }

    bool isSVG()
    {
        impEnsureProcessed();

        return mbIsSVG;
    }

    bool isOwnFormat()
    {
        impEnsureProcessed();

        if(mbIsSVG)
        {
            // xmlns:ooo
            const sal_Int8 aMagic[] = {'x', 'm', 'l', 'n', 's', ':', 'o', 'o', 'o'};
            const sal_Int32 nMagicSize(std::size(aMagic));

            return impCheckForMagic(aMagic, nMagicSize);
        }

        return false;
    }

    bool isImpress()
    {
        impEnsureProcessed();

        if(mbIsSVG)
        {
            // ooo:meta_slides
            const sal_Int8 aMagic[] = {'o', 'o', 'o', ':', 'm', 'e', 't', 'a', '_', 's', 'l', 'i', 'd', 'e', 's'};
            const sal_Int32 nMagicSize(std::size(aMagic));

            return impCheckForMagic(aMagic, nMagicSize);
        }

        return false;
    }
};

}

OUString SAL_CALL SVGFilter::detect(Sequence<PropertyValue>& rDescriptor)
{
    utl::MediaDescriptor aMediaDescriptor(rDescriptor);
    uno::Reference<io::XInputStream> xInput(aMediaDescriptor[utl::MediaDescriptor::PROP_INPUTSTREAM], UNO_QUERY);
    OUString aRetval;

    if (!xInput.is())
    {
        return aRetval;
    }

    try
    {
        SVGFileInfo aSVGFileInfo(xInput);

        if(aSVGFileInfo.isSVG())
        {
            // We have SVG - set default document format to Draw
            aRetval = OUString(constFilterNameDraw);

            if(aSVGFileInfo.isOwnFormat())
            {
                // it's a file that was written/exported by LO
                if(aSVGFileInfo.isImpress())
                {
                    // it was written by Impress export. Set document
                    // format for import to Impress
                    aRetval = OUString(constFilterName);
                }
            }
        }
    }
    catch (css::io::IOException &)
    {
        TOOLS_WARN_EXCEPTION("filter.svg", "");
    }

    return aRetval;
}

//  XServiceInfo
sal_Bool SVGFilter::supportsService(const OUString& sServiceName)
{
    return cppu::supportsService(this, sServiceName);
}
OUString SVGFilter::getImplementationName()
{
    return u"com.sun.star.comp.Draw.SVGFilter"_ustr;
}
css::uno::Sequence< OUString > SVGFilter::getSupportedServiceNames()
{
    return { u"com.sun.star.document.ImportFilter"_ustr,
            u"com.sun.star.document.ExportFilter"_ustr,
            u"com.sun.star.document.ExtendedTypeDetection"_ustr };
}


extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface*
filter_SVGFilter_get_implementation(
    css::uno::XComponentContext* context, css::uno::Sequence<css::uno::Any> const&)
{
    return cppu::acquire(new SVGFilter(context));
}


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
