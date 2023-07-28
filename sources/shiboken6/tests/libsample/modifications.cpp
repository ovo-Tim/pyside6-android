// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "modifications.h"
#include "objecttype.h"

#include <iostream>

Modifications::Modifications()
{
    m_object = new ObjectType();
    m_object->setObjectName("MyObject");
}

Modifications::~Modifications()
{
    delete m_object;
}

Modifications::OverloadedModFunc Modifications::overloaded(int, bool, Point, Point)
{
    return Overloaded_ibPP;
}

Modifications::OverloadedModFunc Modifications::overloaded(int, bool, int, int)
{
    return Overloaded_ibii;
}

Modifications::OverloadedModFunc Modifications::overloaded(int, bool, int, Point)
{
    return Overloaded_ibiP;
}

Modifications::OverloadedModFunc Modifications::overloaded(int, bool, int, bool)
{
    return Overloaded_ibib;
}

Modifications::OverloadedModFunc Modifications::overloaded(int, bool, int, double)
{
    return Overloaded_ibid;
}

void Modifications::argRemoval0(int, bool, int, int)
{
}

void Modifications::argRemoval0(int, bool, int, bool)
{
}

void Modifications::argRemoval1(int, bool, Point, Point, int)
{
}

void Modifications::argRemoval1(int, bool, int, bool)
{
}

void Modifications::argRemoval2(int, bool, Point, Point, int)
{
}

void Modifications::argRemoval3(int, Point, bool, Point, int)
{
}

void Modifications::argRemoval4(int, Point, bool, Point, int)
{
}

void Modifications::argRemoval5(int, bool, Point, Point, int)
{
}

void Modifications::argRemoval5(int, bool, int, bool)
{
}

std::pair<double, double> Modifications::pointToPair(Point pt, bool *ok)
{
    std::pair<double, double> retval(pt.x(), pt.y());
    *ok = true;
    return retval;
}

double Modifications::multiplyPointCoordsPlusValue(bool *ok, Point pt, double value)
{
    double retval = (pt.x() * pt.y()) + value;
    *ok = true;
    return retval;
}

int Modifications::doublePlus(int value, int plus)
{
    return (2 * value) + plus;
}

int Modifications::power(int base, int exponent)
{
    if (exponent == 0)
        return 1;
    int retval = base;
    for (int i = 1; i < exponent; i++)
        retval = retval * base;
    return retval;
}

int Modifications::timesTen(int number)
{
    return number * 10;
}

int Modifications::increment(int number)
{
    return ++number;
}

void Modifications::exclusiveCppStuff()
{
    std::cout << __FUNCTION__ << std::endl;
}

int Modifications::cppMultiply(int a, int b)
{
    return a * b;
}

const char *Modifications::className()
{
    return "Modifications";
}

Point Modifications::sumPointArray(int arraySize, const Point pointArray[])
{
    Point point;
    for (int i = 0; i < arraySize; ++i)
        point = point + pointArray[i];
    return point;
}

int Modifications::getSize(const void *data, int size)
{
    (void)data;
    return size;
}

int Modifications::sumPointCoordinates(const Point *point)
{
    return point->x() + point->y();
}

double Modifications::differenceOfPointCoordinates(const Point *pt, bool *ok)
{
    if (!pt) {
        *ok = false;
        return 0.0;
    }
    *ok = true;
    double result = pt->x() - pt->y();
    if (result < 0)
        result = result * -1.0;
    return result;
}

bool Modifications::nonConversionRuleForArgumentWithDefaultValue(ObjectType **object)
{
    if (object)
        *object = m_object;
    return true;
}

void Modifications::setEnumValue(TestEnum e)
{
    m_enumValue = e;
}

Modifications::TestEnum Modifications::enumValue() const
{
    return m_enumValue;
}

Modifications::TestEnum Modifications::defaultEnumValue() const
{
    return TestEnumValue2;
}

bool Modifications::wasGetAttroCalled() const
{
    return m_getAttroCalled;
}

void Modifications::notifyGetAttroCalled()
{
    m_getAttroCalled = true;
}

bool Modifications::wasSetAttroCalled() const
{
    return m_setAttroCalled;
}

void Modifications::notifySetAttroCalled()
{
    m_setAttroCalled = true;
}
