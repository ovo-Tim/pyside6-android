// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef BASEWRAPPER_P_H
#define BASEWRAPPER_P_H

#include "sbkpython.h"
#include "basewrapper.h"

#include <unordered_map>
#include <set>
#include <string>
#include <vector>
#include <iosfwd>

struct SbkObject;
struct SbkConverter;

namespace Shiboken
{
/**
    * This mapping associates a method and argument of an wrapper object with the wrapper of
    * said argument when it needs the binding to help manage its reference count.
    */
using RefCountMap = std::unordered_multimap<std::string, PyObject *> ;

/// Linked list of SbkBaseWrapper pointers
using ChildrenList = std::set<SbkObject *>;

/// Structure used to store information about object parent and children.
struct ParentInfo
{
    /// Default ctor.
    ParentInfo() : parent(nullptr), hasWrapperRef(false) {}
    /// Pointer to parent object.
    SbkObject *parent;
    /// List of object children.
    ChildrenList children;
    /// has internal ref
    bool hasWrapperRef;
};

} // namespace Shiboken

extern "C"
{

/**
 * \internal
 * Private data for SbkBaseWrapper
 */
struct SbkObjectPrivate
{
    /// Pointer to the C++ class.
    void ** cptr;
    /// True when Python is responsible for freeing the used memory.
    unsigned int hasOwnership : 1;
    /// This is true when the C++ class of the wrapped object has a virtual destructor AND was created by Python.
    unsigned int containsCppWrapper : 1;
    /// Marked as false when the object is lost to C++ and the binding can not know if it was deleted or not.
    unsigned int validCppObject : 1;
    /// Marked as true when the object constructor was called
    unsigned int cppObjectCreated : 1;
    /// PYSIDE-1470: Marked as true if this is the Q*Application singleton.
    /// This bit allows app deletion from shiboken?.delete() .
    unsigned int isQAppSingleton : 1;
    /// Information about the object parents and children, may be null.
    Shiboken::ParentInfo *parentInfo;
    /// Manage reference count of objects that are referred to but not owned from.
    Shiboken::RefCountMap *referredObjects;

    ~SbkObjectPrivate()
    {
        delete parentInfo;
        parentInfo = nullptr;
        delete referredObjects;
        referredObjects = nullptr;
    }
};

// TODO-CONVERTERS: to be deprecated/removed
/// The type behaviour was not defined yet
#define BEHAVIOUR_UNDEFINED 0
/// The type is a value type
#define BEHAVIOUR_VALUETYPE 1
/// The type is an object type
#define BEHAVIOUR_OBJECTTYPE 2

struct SbkObjectTypePrivate
{
    SbkConverter *converter;
    int *mi_offsets;
    MultipleInheritanceInitFunction mi_init;

    /// Special cast function, null if this class doesn't have multiple inheritance.
    SpecialCastFunction mi_specialcast;
    TypeDiscoveryFuncV2 type_discovery;
    /// Pointer to a function responsible for deletion of the C++ instance calling the proper destructor.
    ObjectDestructor cpp_dtor;
    /// C++ name
    char *original_name;
    /// Type user data
    void *user_data;
    DeleteUserDataFunc d_func;
    void (*subtype_init)(PyTypeObject *, PyObject *, PyObject *);
    const char **propertyStrings;
    const char **enumFlagInfo;
    PyObject *enumFlagsDict;
    PyObject *enumTypeDict;

    /// True if this type holds two or more C++ instances, e.g.: a Python class which inherits from two C++ classes.
    unsigned int is_multicpp : 1;
    /// True if this type was defined by the user.
    unsigned int is_user_type : 1;
    /// Tells is the type is a value type or an object-type, see BEHAVIOUR_ *constants.
    unsigned int type_behaviour : 2;
    unsigned int delete_in_main_thread : 1;
};


} // extern "C"

namespace Shiboken
{

/**
 * \internal
 * Data required to invoke a C++ destructor
 */
struct DestructorEntry
{
    ObjectDestructor destructor;
    void *cppInstance;
};

/**
 * Utility function used to transform a PyObject that implements sequence protocol into a std::list.
 **/
std::vector<SbkObject *> splitPyObject(PyObject *pyObj);

/**
*   Visitor class used by walkOnClassHierarchy function.
*/
class HierarchyVisitor
{
public:
    HierarchyVisitor(const HierarchyVisitor &) = delete;
    HierarchyVisitor(HierarchyVisitor &&) = delete;
    HierarchyVisitor &operator=(const HierarchyVisitor &) = delete;
    HierarchyVisitor &operator=(HierarchyVisitor &&) = delete;

    HierarchyVisitor();
    virtual ~HierarchyVisitor();

    virtual bool visit(PyTypeObject *node) = 0; // return true to terminate
};

class BaseCountVisitor : public HierarchyVisitor
{
public:
    bool visit(PyTypeObject *) override;

    int count() const { return m_count; }

private:
    int m_count = 0;
};

class BaseAccumulatorVisitor : public HierarchyVisitor
{
public:
    using Result = std::vector<PyTypeObject *>;

    bool visit(PyTypeObject *node) override;

    Result bases() const { return m_bases; }

private:
    Result m_bases;
};

class GetIndexVisitor : public HierarchyVisitor
{
public:
    explicit GetIndexVisitor(PyTypeObject *desiredType) : m_desiredType(desiredType) {}

    bool visit(PyTypeObject *node) override;

    int index() const { return m_index; }

private:
    int m_index = -1;
    PyTypeObject *m_desiredType;
};

/// Collect destructors and C++ instances of each C++ object held by a Python
/// object
class DtorAccumulatorVisitor : public HierarchyVisitor
{
public:
    explicit DtorAccumulatorVisitor(SbkObject *pyObj) : m_pyObject(pyObj) {}

    bool visit(PyTypeObject *node) override;

    using DestructorEntries = std::vector<DestructorEntry>;

    const DestructorEntries &entries() const { return m_entries; }

private:
    DestructorEntries m_entries;
    SbkObject *m_pyObject;
};

/// \internal Internal function used to walk on classes inheritance trees.
/**
*   Walk on class hierarchy using a DFS algorithm.
*   For each pure Shiboken type found, HierarchyVisitor::visit is called and the algorithm
*   considers all children of this type as visited.
*/
bool walkThroughClassHierarchy(PyTypeObject *currentType, HierarchyVisitor *visitor);

inline int getTypeIndexOnHierarchy(PyTypeObject *baseType, PyTypeObject *desiredType)
{
    GetIndexVisitor visitor(desiredType);
    walkThroughClassHierarchy(baseType, &visitor);
    return visitor.index();
}

inline int getNumberOfCppBaseClasses(PyTypeObject *baseType)
{
    BaseCountVisitor visitor;
    walkThroughClassHierarchy(baseType, &visitor);
    return visitor.count();
}

inline std::vector<PyTypeObject *> getCppBaseClasses(PyTypeObject *baseType)
{
    BaseAccumulatorVisitor visitor;
    walkThroughClassHierarchy(baseType, &visitor);
    return visitor.bases();
}

namespace Object
{
/**
*   Decrements the reference counters of every object referred by self.
*   \param self    the wrapper instance that keeps references to other objects.
*/
void clearReferences(SbkObject *self);

/**
 * Destroy internal data
 **/
void deallocData(SbkObject *self, bool doCleanup);


void _debugFormat(std::ostream &str, SbkObject *self);
} // namespace Object

} // namespace Shiboken

#endif
