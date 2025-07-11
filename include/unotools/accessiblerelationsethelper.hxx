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

#ifndef INCLUDED_UNOTOOLS_ACCESSIBLERELATIONSETHELPER_HXX
#define INCLUDED_UNOTOOLS_ACCESSIBLERELATIONSETHELPER_HXX

#include <unotools/unotoolsdllapi.h>

#include <com/sun/star/accessibility/AccessibleRelationType.hpp>
#include <com/sun/star/accessibility/XAccessibleRelationSet.hpp>
#include <cppuhelper/implbase.hxx>
#include <mutex>
#include <vector>

using css::accessibility::AccessibleRelationType;

//= XAccessibleRelationSet helper classes

//... namespace utl .......................................................
namespace utl
{
/** @descr
        This base class provides an implementation of the
        <code>AccessibleRelationSet</code> service.
*/
class UNOTOOLS_DLLPUBLIC AccessibleRelationSetHelper final
    : public cppu::WeakImplHelper<css::accessibility::XAccessibleRelationSet>
{
public:
    AccessibleRelationSetHelper();

    css::uno::Reference<css::accessibility::XAccessibleRelationSet> Clone() const;

private:
    AccessibleRelationSetHelper(const AccessibleRelationSetHelper& rHelper);
    virtual ~AccessibleRelationSetHelper() override;

public:
    //=====  XAccessibleRelationSet  ==========================================

    /** Returns the number of relations in this relation set.

        @return
            Returns the number of relations or zero if there are none.
    */
    virtual sal_Int32 SAL_CALL getRelationCount() override;

    /** Returns the relation of this relation set that is specified by
        the given index.

        @param nIndex
            This index specifies the relatio to return.

        @return
            For a valid index, i.e. inside the range 0 to the number of
            relations minus one, the returned value is the requested
            relation.  If the index is invalid then the returned relation
            has the type INVALID.

    */
    virtual css::accessibility::AccessibleRelation SAL_CALL getRelation(sal_Int32 nIndex) override;

    /** Tests whether the relation set contains a relation matching the
        specified key.

        @param eRelationType
            The type of relation to look for in this set of relations.

        @return
            Returns <TRUE/> if there is a (at least one) relation of the
            given type and <FALSE/> if there is no such relation in the set.
    */
    virtual sal_Bool SAL_CALL
    containsRelation(css::accessibility::AccessibleRelationType eRelationType) override;

    /** Retrieve and return the relation with the given relation type.

        @param eRelationType
            The type of the relation to return.

        @return
            If a relation with the given type could be found than (a copy
            of) this relation is returned.  Otherwise a relation with the
            type INVALID is returned.
    */
    virtual css::accessibility::AccessibleRelation SAL_CALL
    getRelationByType(AccessibleRelationType eRelationType) override;

    /// @throws uno::RuntimeException
    void AddRelation(const css::accessibility::AccessibleRelation& rRelation);

    //=====  XTypeProvider  ===================================================

    /** Returns a sequence of all supported interfaces.
    */
    virtual css::uno::Sequence<css::uno::Type> SAL_CALL getTypes() override;

private:
    /// Mutex guarding this object.
    mutable std::mutex maMutex;
    /// The implementation of this helper interface.
    std::vector<css::accessibility::AccessibleRelation> maRelations;
};
}
//... namespace utl .......................................................
#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
