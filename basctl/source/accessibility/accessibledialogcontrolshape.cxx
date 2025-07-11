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

#include <accessibledialogcontrolshape.hxx>
#include <baside3.hxx>
#include <dlgeddef.hxx>
#include <dlgedview.hxx>
#include <dlgedobj.hxx>
#include <com/sun/star/awt/XVclWindowPeer.hpp>
#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <com/sun/star/accessibility/AccessibleRole.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <cppuhelper/supportsservice.hxx>
#include <unotools/accessiblerelationsethelper.hxx>
#include <toolkit/awt/vclxfont.hxx>
#include <toolkit/helper/vclunohelper.hxx>
#include <comphelper/accessiblecontexthelper.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <vcl/svapp.hxx>
#include <vcl/settings.hxx>
#include <vcl/unohelp.hxx>
#include <i18nlangtag/languagetag.hxx>

namespace basctl
{

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::accessibility;
using namespace ::comphelper;




AccessibleDialogControlShape::AccessibleDialogControlShape (DialogWindow* pDialogWindow, DlgEdObj* pDlgEdObj)
    :m_pDialogWindow( pDialogWindow )
    ,m_pDlgEdObj( pDlgEdObj )
{
    if ( m_pDlgEdObj )
        m_xControlModel.set( m_pDlgEdObj->GetUnoControlModel(), UNO_QUERY );

    if ( m_xControlModel.is() )
        m_xControlModel->addPropertyChangeListener( OUString(), static_cast< beans::XPropertyChangeListener* >( this ) );

    m_bFocused = IsFocused();
    m_bSelected = IsSelected();
    m_aBounds = GetBounds();
}


AccessibleDialogControlShape::~AccessibleDialogControlShape()
{
    if ( m_xControlModel.is() )
        m_xControlModel->removePropertyChangeListener( OUString(), static_cast< beans::XPropertyChangeListener* >( this ) );
}


bool AccessibleDialogControlShape::IsFocused() const
{
    bool bFocused = false;
    if ( m_pDialogWindow )
    {
        SdrView& rView = m_pDialogWindow->GetView();
        if (rView.IsObjMarked(m_pDlgEdObj) && rView.GetMarkedObjectList().GetMarkCount() == 1)
            bFocused = true;
    }

    return bFocused;
}


bool AccessibleDialogControlShape::IsSelected() const
{
    if ( m_pDialogWindow )
        return m_pDialogWindow->GetView().IsObjMarked(m_pDlgEdObj);
    return false;
}


void AccessibleDialogControlShape::SetFocused( bool bFocused )
{
    if ( m_bFocused != bFocused )
    {
        Any aOldValue, aNewValue;
        if ( m_bFocused )
            aOldValue <<= AccessibleStateType::FOCUSED;
        else
            aNewValue <<= AccessibleStateType::FOCUSED;
        m_bFocused = bFocused;
        NotifyAccessibleEvent( AccessibleEventId::STATE_CHANGED, aOldValue, aNewValue );
    }
}


void AccessibleDialogControlShape::SetSelected( bool bSelected )
{
    if ( m_bSelected != bSelected )
    {
        Any aOldValue, aNewValue;
        if ( m_bSelected )
            aOldValue <<= AccessibleStateType::SELECTED;
        else
            aNewValue <<= AccessibleStateType::SELECTED;
        m_bSelected = bSelected;
        NotifyAccessibleEvent( AccessibleEventId::STATE_CHANGED, aOldValue, aNewValue );
    }
}


awt::Rectangle AccessibleDialogControlShape::GetBounds() const
{
    awt::Rectangle aBounds( 0, 0, 0, 0 );
    if ( m_pDlgEdObj )
    {
        // get the bounding box of the shape in logic units
        tools::Rectangle aRect = m_pDlgEdObj->GetSnapRect();

        if ( m_pDialogWindow )
        {
            // transform coordinates relative to the parent
            MapMode aMap = m_pDialogWindow->GetMapMode();
            Point aOrg = aMap.GetOrigin();
            aRect.Move( aOrg.X(), aOrg.Y() );

            // convert logic units to pixel
            aRect = m_pDialogWindow->LogicToPixel( aRect, MapMode(MapUnit::Map100thMM) );

            // clip the shape's bounding box with the bounding box of its parent
            tools::Rectangle aParentRect( Point( 0, 0 ), m_pDialogWindow->GetSizePixel() );
            aRect = aRect.GetIntersection( aParentRect );
            aBounds = vcl::unohelper::ConvertToAWTRect(aRect);
        }
    }

    return aBounds;
}


void AccessibleDialogControlShape::SetBounds( const awt::Rectangle& aBounds )
{
    if ( m_aBounds.X != aBounds.X || m_aBounds.Y != aBounds.Y || m_aBounds.Width != aBounds.Width || m_aBounds.Height != aBounds.Height )
    {
        m_aBounds = aBounds;
        NotifyAccessibleEvent( AccessibleEventId::BOUNDRECT_CHANGED, Any(), Any() );
    }
}


vcl::Window* AccessibleDialogControlShape::GetWindow() const
{
    vcl::Window* pWindow = nullptr;
    if ( m_pDlgEdObj )
    {
        Reference< awt::XControl > xControl = m_pDlgEdObj->GetControl();
        if ( xControl.is() )
            pWindow = VCLUnoHelper::GetWindow( xControl->getPeer() );
    }

    return pWindow;
}


OUString AccessibleDialogControlShape::GetModelStringProperty( OUString const & pPropertyName )
{
    OUString sReturn;

    try
    {
        if ( m_xControlModel.is() )
        {
            Reference< XPropertySetInfo > xInfo = m_xControlModel->getPropertySetInfo();
            if ( xInfo.is() && xInfo->hasPropertyByName( pPropertyName ) )
                m_xControlModel->getPropertyValue( pPropertyName ) >>= sReturn;
        }
    }
    catch ( const Exception& )
    {
        TOOLS_WARN_EXCEPTION( "basctl", "AccessibleDialogControlShape::GetModelStringProperty" );
    }

    return sReturn;
}


void AccessibleDialogControlShape::FillAccessibleStateSet( sal_Int64& rStateSet )
{
    rStateSet |= AccessibleStateType::ENABLED;

    rStateSet |= AccessibleStateType::VISIBLE;

    rStateSet |= AccessibleStateType::SHOWING;

    rStateSet |= AccessibleStateType::FOCUSABLE;

    if ( IsFocused() )
        rStateSet |= AccessibleStateType::FOCUSED;

    rStateSet |= AccessibleStateType::SELECTABLE;

    if ( IsSelected() )
        rStateSet |= AccessibleStateType::SELECTED;

    rStateSet |= AccessibleStateType::RESIZABLE;
}

awt::Rectangle AccessibleDialogControlShape::implGetBounds()
{
    return GetBounds();
}

// XComponent
void AccessibleDialogControlShape::disposing()
{
    OAccessible::disposing();

    m_pDialogWindow = nullptr;
    m_pDlgEdObj = nullptr;

    if ( m_xControlModel.is() )
        m_xControlModel->removePropertyChangeListener( OUString(), static_cast< beans::XPropertyChangeListener* >( this ) );
    m_xControlModel.clear();
}


// XEventListener


void AccessibleDialogControlShape::disposing( const lang::EventObject& )
{
    if ( m_xControlModel.is() )
        m_xControlModel->removePropertyChangeListener( OUString(), static_cast< beans::XPropertyChangeListener* >( this ) );
    m_xControlModel.clear();
}


// XPropertyChangeListener


void AccessibleDialogControlShape::propertyChange( const beans::PropertyChangeEvent& rEvent )
{
    if ( rEvent.PropertyName == DLGED_PROP_NAME )
    {
        NotifyAccessibleEvent( AccessibleEventId::NAME_CHANGED, rEvent.OldValue, rEvent.NewValue );
    }
    else if ( rEvent.PropertyName == DLGED_PROP_POSITIONX ||
              rEvent.PropertyName == DLGED_PROP_POSITIONY ||
              rEvent.PropertyName == DLGED_PROP_WIDTH ||
              rEvent.PropertyName == DLGED_PROP_HEIGHT )
    {
        SetBounds( GetBounds() );
    }
    else if ( rEvent.PropertyName == DLGED_PROP_BACKGROUNDCOLOR ||
              rEvent.PropertyName == DLGED_PROP_TEXTCOLOR ||
              rEvent.PropertyName == DLGED_PROP_TEXTLINECOLOR )
    {
        NotifyAccessibleEvent( AccessibleEventId::VISIBLE_DATA_CHANGED, Any(), Any() );
    }
}

// XServiceInfo
OUString AccessibleDialogControlShape::getImplementationName()
{
    return u"com.sun.star.comp.basctl.AccessibleShape"_ustr;
}

sal_Bool AccessibleDialogControlShape::supportsService( const OUString& rServiceName )
{
    return cppu::supportsService(this, rServiceName);
}

Sequence< OUString > AccessibleDialogControlShape::getSupportedServiceNames()
{
    return { u"com.sun.star.drawing.AccessibleShape"_ustr };
}

// XAccessibleContext
sal_Int64 AccessibleDialogControlShape::getAccessibleChildCount()
{
    return 0;
}


Reference< XAccessible > AccessibleDialogControlShape::getAccessibleChild( sal_Int64 i )
{
    OExternalLockGuard aGuard( this );

    if ( i < 0 || i >= getAccessibleChildCount() )
        throw IndexOutOfBoundsException();

    return Reference< XAccessible >();
}


Reference< XAccessible > AccessibleDialogControlShape::getAccessibleParent(  )
{
    OExternalLockGuard aGuard( this );

    Reference< XAccessible > xParent;
    if ( m_pDialogWindow )
        xParent = m_pDialogWindow->GetAccessible();

    return xParent;
}

sal_Int16 AccessibleDialogControlShape::getAccessibleRole(  )
{
    OExternalLockGuard aGuard( this );

    return AccessibleRole::SHAPE;
}


OUString AccessibleDialogControlShape::getAccessibleDescription(  )
{
    OExternalLockGuard aGuard( this );

    return GetModelStringProperty( u"HelpText"_ustr );
}


OUString AccessibleDialogControlShape::getAccessibleName(  )
{
    OExternalLockGuard aGuard( this );

    return GetModelStringProperty( u"Name"_ustr );
}


Reference< XAccessibleRelationSet > AccessibleDialogControlShape::getAccessibleRelationSet(  )
{
    OExternalLockGuard aGuard( this );

    return new utl::AccessibleRelationSetHelper;
}


sal_Int64 AccessibleDialogControlShape::getAccessibleStateSet(  )
{
    OExternalLockGuard aGuard( this );

    sal_Int64 nStateSet = 0;

    if (isAlive())
    {
        FillAccessibleStateSet( nStateSet );
    }
    else
    {
        nStateSet |= AccessibleStateType::DEFUNC;
    }

    return nStateSet;
}


Locale AccessibleDialogControlShape::getLocale(  )
{
    OExternalLockGuard aGuard( this );

    return Application::GetSettings().GetLanguageTag().getLocale();
}


// XAccessibleComponent


Reference< XAccessible > AccessibleDialogControlShape::getAccessibleAtPoint( const awt::Point& )
{
    OExternalLockGuard aGuard( this );

    return Reference< XAccessible >();
}


void AccessibleDialogControlShape::grabFocus(  )
{
    // no focus for shapes
}


sal_Int32 AccessibleDialogControlShape::getForeground(  )
{
    OExternalLockGuard aGuard( this );

    Color nColor;
    vcl::Window* pWindow = GetWindow();
    if ( pWindow )
    {
        if ( pWindow->IsControlForeground() )
            nColor = pWindow->GetControlForeground();
        else
        {
            vcl::Font aFont;
            if ( pWindow->IsControlFont() )
                aFont = pWindow->GetControlFont();
            else
                aFont = pWindow->GetFont();
            nColor = aFont.GetColor();
        }
    }

    return sal_Int32(nColor);
}


sal_Int32 AccessibleDialogControlShape::getBackground(  )
{
    OExternalLockGuard aGuard( this );

    Color nColor;
    vcl::Window* pWindow = GetWindow();
    if ( pWindow )
    {
        if ( pWindow->IsControlBackground() )
            nColor = pWindow->GetControlBackground();
        else
            nColor = pWindow->GetBackground().GetColor();
    }

    return sal_Int32(nColor);
}


// XAccessibleExtendedComponent

OUString AccessibleDialogControlShape::getToolTipText(  )
{
    OExternalLockGuard aGuard( this );

    OUString sText;
    vcl::Window* pWindow = GetWindow();
    if ( pWindow )
        sText = pWindow->GetQuickHelpText();

    return sText;
}


} // namespace basctl

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
