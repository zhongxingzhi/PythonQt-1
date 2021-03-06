/*
*
*  Copyright (C) 2010 MeVis Medical Solutions AG All Rights Reserved.
*
*  This library is free software; you can redistribute it and/or
*  modify it under the terms of the GNU Lesser General Public
*  License as published by the Free Software Foundation; either
*  version 2.1 of the License, or (at your option) any later version.
*
*  This library is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*  Lesser General Public License for more details.
*
*  Further, this software is distributed without any warranty that it is
*  free of the rightful claim of any third person regarding infringement
*  or the like.  Any license provided herein, whether implied or
*  otherwise, applies only to this software file.  Patent licenses, if
*  any, provided herein do not apply to combinations of this program with
*  other software, or any other product whatsoever.
*
*  You should have received a copy of the GNU Lesser General Public
*  License along with this library; if not, write to the Free Software
*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*  Contact information: MeVis Medical Solutions AG, Universitaetsallee 29,
*  28359 Bremen, Germany or:
*
*  http://www.mevis.de
*
*/

//----------------------------------------------------------------------------------
/*!
// \file    PythonQtConversion.cpp
// \author  Florian Link
// \author  Last changed by $Author: florian $
// \date    2006-05
*/
//----------------------------------------------------------------------------------

#include "PythonQtConversion.h"
#include "PythonQtVariants.h"
#include "PythonQtBoolResult.h"
#include <QDateTime>
#include <QTime>
#include <QDate>
#include <climits>

PythonQtValueStorage<qint64, 128>  PythonQtConv::global_valueStorage;
PythonQtValueStorage<void*, 128>   PythonQtConv::global_ptrStorage;
PythonQtValueStorageWithCleanup<QVariant, 128> PythonQtConv::global_variantStorage;

QHash<int, PythonQtConvertMetaTypeToPythonCB*> PythonQtConv::_metaTypeToPythonConverters;
QHash<int, PythonQtConvertPythonToMetaTypeCB*> PythonQtConv::_pythonToMetaTypeConverters;

PyObject* PythonQtConv::GetPyBool(bool val)
{
  PyObject* r = val?Py_True:Py_False;
  Py_INCREF(r);
  return r;
}

PyObject* PythonQtConv::ConvertQtValueToPython(const PythonQtMethodInfo::ParameterInfo& info, const void* data) {
  // is it an enum value?
  if (info.enumWrapper) {
    if (info.pointerCount==0) {
      return PythonQtPrivate::createEnumValueInstance(info.enumWrapper, *((unsigned int*)data));
    } else {
      // we do not support pointers to enums (who needs them?)
      Py_INCREF(Py_None);
      return Py_None;
    }
  }

  if (info.typeId == QMetaType::Void) {
    Py_INCREF(Py_None);
    return Py_None;
  } else if ((info.pointerCount == 1) && (info.typeId == QMetaType::Char)) {
    // a char ptr will probably be a null terminated string, so we support that:
    char* charPtr = *((char**)data);
    if (charPtr) {
      return PyString_FromString(charPtr);
    } else {
      Py_INCREF(Py_None);
      return Py_None;
    }
  } else if ((info.typeId == PythonQtMethodInfo::Unknown || info.typeId >= QMetaType::User) &&
             info.isQList && (info.innerNamePointerCount == 1)) {
    // it is a QList<Obj*> template:
    QList<void*>* listPtr = NULL;
    if (info.pointerCount == 1) {
      listPtr = *((QList<void*>**)data);
    } else if (info.pointerCount == 0) {
      listPtr = (QList<void*>*)data;
    }
    if (listPtr) {
      return ConvertQListOfPointerTypeToPythonList(listPtr, info.innerName);
    } else {
      return NULL;
    }
  }

  if (info.typeId >= QMetaType::User) {
    // if a converter is registered, we use is:
    PythonQtConvertMetaTypeToPythonCB* converter = _metaTypeToPythonConverters.value(info.typeId);
    if (converter) {
      return (*converter)(info.pointerCount==0?data:*((void**)data), info.typeId);
    }
  }

  // special handling did not match, so we convert the usual way (either pointer or value version):
  if (info.pointerCount == 1) {
    // convert the pointer to a Python Object (we can handle ANY C++ object, in the worst case we just know the type and the pointer)
    return PythonQt::priv()->wrapPtr(*((void**)data), info.name);
  } else if (info.pointerCount == 0) {
    if (info.typeId != PythonQtMethodInfo::Unknown) {
      // handle values that are const& or by value and have a metatype
      return ConvertQtValueToPythonInternal(info.typeId, data);
    } else {
      // the type does not have a typeid, we need to make a copy using the copy constructor
      PythonQtClassInfo* classInfo = PythonQt::priv()->getClassInfo(info.name);
      if (classInfo) {
        PyObject* result = classInfo->copyObject((void*)data);
        if (result) {
          return result;
        }
      }
    }
  }
  Py_INCREF(Py_None);
  return Py_None;
}

PyObject* PythonQtConv::ConvertQtValueToPythonInternal(int type, const void* data) {
  switch (type) {
  case QMetaType::Void:
    Py_INCREF(Py_None);
    return Py_None;
  case QMetaType::Char:
    return PyLong_FromLong(*((char*)data));
  case QMetaType::UChar:
    return PyLong_FromLong(*((unsigned char*)data));
  case QMetaType::Short:
    return PyLong_FromLong(*((short*)data));
  case QMetaType::UShort:
    return PyLong_FromLong(*((unsigned short*)data));
  case QMetaType::Long:
    return PyLong_FromLong(*((long*)data));
  case QMetaType::ULong:
    // does not fit into simple int of python
    return PyLong_FromUnsignedLong(*((unsigned long*)data));
  case QMetaType::Bool:
    return PythonQtConv::GetPyBool(*((bool*)data));
  case QMetaType::Int:
    return PyLong_FromLong(*((int*)data));
  case QMetaType::UInt:
    // does not fit into simple int of python
    return PyLong_FromUnsignedLong(*((unsigned int*)data));
  case QMetaType::QChar:
    return PyLong_FromLong(*((unsigned short*)data));
  case QMetaType::Float:
    return PyFloat_FromDouble(*((float*)data));
  case QMetaType::Double:
    return PyFloat_FromDouble(*((double*)data));
  case QMetaType::LongLong:
    return PyLong_FromLongLong(*((qint64*)data));
  case QMetaType::ULongLong:
    return PyLong_FromUnsignedLongLong(*((quint64*)data));
      // implicit conversion from QByteArray to str has been removed:
  //case QMetaType::QByteArray: {
  //  QByteArray* v = (QByteArray*) data;
  //  return PyBytes_FromStringAndSize(*v, v->size());
  //                            }
  case QMetaType::QVariantHash:
    return PythonQtConv::QVariantHashToPyObject(*((QVariantHash*)data));
  case QMetaType::QVariantMap:
    return PythonQtConv::QVariantMapToPyObject(*((QVariantMap*)data));
  case QMetaType::QVariantList:
    return PythonQtConv::QVariantListToPyObject(*((QVariantList*)data));
  case QMetaType::QString:
    return PythonQtConv::QStringToPyObject(*((QString*)data));
  case QMetaType::QStringList:
    return PythonQtConv::QStringListToPyObject(*((QStringList*)data));

  case PythonQtMethodInfo::Variant:
#if QT_VERSION >= 0x040800
  case QMetaType::QVariant:
#endif
    return PythonQtConv::QVariantToPyObject(*((QVariant*)data));
  case QMetaType::QObjectStar:
#if( QT_VERSION < QT_VERSION_CHECK(5,0,0) )
  case QMetaType::QWidgetStar:
#endif
    return PythonQt::priv()->wrapQObject(*((QObject**)data));

  default:
    if (PythonQt::priv()->isPythonQtObjectPtrMetaId(type)) {
      // special case, it is a PythonQtObjectPtr which contains a PyObject, take it directly:
      PyObject* o = ((PythonQtObjectPtr*)data)->object();
      Py_INCREF(o);
      return o;
    } else {
      if (type > 0) {
        return createCopyFromMetaType(type, data);
      } else {
        std::cerr << "Unknown type that can not be converted to Python: " << type << ", in " << __FILE__ << ":" << __LINE__ << std::endl;
      }
    }
  }
  Py_INCREF(Py_None);
  return Py_None;
 }

 void* PythonQtConv::CreateQtReturnValue(const PythonQtMethodInfo::ParameterInfo& info) {
   void* ptr = NULL;
   if (info.pointerCount>1) {
     return NULL;
   } else if (info.pointerCount==1) {
     PythonQtValueStorage_ADD_VALUE(global_ptrStorage, void*, NULL, ptr);
   } else if (info.enumWrapper) {
     // create enum return value
     PythonQtValueStorage_ADD_VALUE(PythonQtConv::global_valueStorage, long, 0, ptr);
   } else {
     switch (info.typeId) {
     case QMetaType::Char:
     case QMetaType::UChar:
     case QMetaType::Short:
     case QMetaType::UShort:
     case QMetaType::Long:
     case QMetaType::ULong:
     case QMetaType::Bool:
     case QMetaType::Int:
     case QMetaType::UInt:
     case QMetaType::QChar:
     case QMetaType::Float:
     case QMetaType::Double:
     case QMetaType::LongLong:
     case QMetaType::ULongLong:
       PythonQtValueStorage_ADD_VALUE(global_valueStorage, qint64, 0, ptr);
       break;
     case PythonQtMethodInfo::Variant:
       PythonQtValueStorage_ADD_VALUE(global_variantStorage, QVariant, 0, ptr);
       // return the ptr to the variant
       break;
     default:
       // check if we have a QList of pointers, which we can circumvent with a QList<void*>
       if (info.isQList && (info.innerNamePointerCount == 1)) {
         static int id = QMetaType::type("QList<void*>");
         PythonQtValueStorage_ADD_VALUE(global_variantStorage, QVariant, QVariant::Type(id), ptr);
         // return the constData pointer that will be filled with the result value later on
         ptr = (void*)((QVariant*)ptr)->constData();
       }

       if (!ptr && info.typeId != PythonQtMethodInfo::Unknown) {
         // everything else is stored in a QVariant, if we know the meta type...
         PythonQtValueStorage_ADD_VALUE(global_variantStorage, QVariant, QVariant::Type(info.typeId), ptr);
         // return the constData pointer that will be filled with the result value later on
         ptr = (void*)((QVariant*)ptr)->constData();
       }
     }
   }
   return ptr;
 }

 void* PythonQtConv::castWrapperTo(PythonQtInstanceWrapper* wrapper, const QByteArray& className, bool& ok)
 {
   void* object;
   if (wrapper->classInfo()->isCPPWrapper()) {
     object = wrapper->_wrappedPtr;
   } else {
     QObject* tmp = wrapper->_obj;
     object = tmp;
   }
   if (object) {
     // if we can be upcasted to the given name, we pass the casted pointer in:
     object = wrapper->classInfo()->castTo(object, className);
     ok = object!=NULL;
   } else {
     // if it is a NULL ptr, we need to check if it inherits, so that we might pass the NULL ptr
     ok = wrapper->classInfo()->inherits(className);
   }
   return object;
 }

void* PythonQtConv::handlePythonToQtAutoConversion(int typeId, PyObject* obj, void* alreadyAllocatedCPPObject)
{
  void* ptr = alreadyAllocatedCPPObject;

  static int penId = QMetaType::type("QPen");
  static int brushId = QMetaType::type("QBrush");
  static int cursorId = QMetaType::type("QCursor");
  static int colorId = QMetaType::type("QColor");
  static PyObject* qtGlobalColorEnum = PythonQtClassInfo::findEnumWrapper("Qt::GlobalColor", NULL);
  if (typeId == cursorId) {
    static PyObject* qtCursorShapeEnum = PythonQtClassInfo::findEnumWrapper("Qt::CursorShape", NULL);
    if ((PyObject*)obj->ob_type == qtCursorShapeEnum) {
      Qt::CursorShape val = (Qt::CursorShape)PyInt_AsLong(obj);
      if (!ptr) {
        PythonQtValueStorage_ADD_VALUE(global_variantStorage, QVariant, QCursor(), ptr);
        ptr = (void*)((QVariant*)ptr)->constData();
      }
      *((QCursor*)ptr) = QCursor(val);
      return ptr;
    }
  } else if (typeId == penId) {
    // brushes can be created from QColor and from Qt::GlobalColor (and from brushes, but that's the default)
    static PyObject* qtColorClass = PythonQt::priv()->getClassInfo("QColor")->pythonQtClassWrapper();
    if ((PyObject*)obj->ob_type == qtGlobalColorEnum) {
      Qt::GlobalColor val = (Qt::GlobalColor)PyInt_AsLong(obj);
      if (!ptr) {
        PythonQtValueStorage_ADD_VALUE(global_variantStorage, QVariant, QPen(), ptr);
        ptr = (void*)((QVariant*)ptr)->constData();
      }
      *((QPen*)ptr) = QPen(QColor(val));
      return ptr;
    } else if ((PyObject*)obj->ob_type == qtColorClass) {
      if (!ptr) {
        PythonQtValueStorage_ADD_VALUE(global_variantStorage, QVariant, QPen(), ptr);
        ptr = (void*)((QVariant*)ptr)->constData();
      }
      *((QPen*)ptr) = QPen(*((QColor*)((PythonQtInstanceWrapper*)obj)->_wrappedPtr));
      return ptr;
    }
  } else if (typeId == brushId) {
    // brushes can be created from QColor and from Qt::GlobalColor (and from brushes, but that's the default)
    static PyObject* qtColorClass = PythonQt::priv()->getClassInfo("QColor")->pythonQtClassWrapper();
    if ((PyObject*)obj->ob_type == qtGlobalColorEnum) {
      Qt::GlobalColor val = (Qt::GlobalColor)PyInt_AsLong(obj);
      if (!ptr) {
        PythonQtValueStorage_ADD_VALUE(global_variantStorage, QVariant, QBrush(), ptr);
        ptr = (void*)((QVariant*)ptr)->constData();
      }
      *((QBrush*)ptr) = QBrush(QColor(val));
      return ptr;
    } else if ((PyObject*)obj->ob_type == qtColorClass) {
      if (!ptr) {
        PythonQtValueStorage_ADD_VALUE(global_variantStorage, QVariant, QBrush(), ptr);
        ptr = (void*)((QVariant*)ptr)->constData();
      }
      *((QBrush*)ptr) = QBrush(*((QColor*)((PythonQtInstanceWrapper*)obj)->_wrappedPtr));
      return ptr;
    }
  } else if (typeId == colorId) {
    // colors can be created from Qt::GlobalColor (and from colors, but that's the default)
    if ((PyObject*)obj->ob_type == qtGlobalColorEnum) {
      Qt::GlobalColor val = (Qt::GlobalColor)PyInt_AsLong(obj);
      if (!ptr) {
        PythonQtValueStorage_ADD_VALUE(global_variantStorage, QVariant, QColor(), ptr);
        ptr = (void*)((QVariant*)ptr)->constData();
      }
      *((QColor*)ptr) = QColor(val);
      return ptr;
    }
  }
  return NULL;
}

void* PythonQtConv::ConvertPythonToQt(const PythonQtMethodInfo::ParameterInfo& info, PyObject* obj, bool strict, PythonQtClassInfo* /*classInfo*/, void* alreadyAllocatedCPPObject)
 {
   bool ok = false;
   void* ptr = NULL;

   // autoconversion of QPen/QBrush/QCursor/QColor from different type
   if (info.pointerCount==0 && !strict) {
     ptr = handlePythonToQtAutoConversion(info.typeId, obj, alreadyAllocatedCPPObject);
     if (ptr) {
       return ptr;
     }
   }
   if (info.pointerCount==1 && PythonQtBoolResult_Check(obj) && info.typeId == QMetaType::Bool) {
     PythonQtBoolResultObject* boolResul = (PythonQtBoolResultObject*)obj;
     // store the wrapped pointer in an extra pointer and let ptr point to the extra pointer
     PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_ptrStorage, void*, &boolResul->_value, ptr);
     return ptr;
   }

   if (PyObject_TypeCheck(obj, &PythonQtInstanceWrapper_Type) && info.typeId != PythonQtMethodInfo::Variant) {
     // if we have a Qt wrapper object and if we do not need a QVariant, we do the following:
     // (the Variant case is handled below in a switch)

     // a C++ wrapper (can be passed as pointer or reference)
     PythonQtInstanceWrapper* wrap = (PythonQtInstanceWrapper*)obj;
     void* object = castWrapperTo(wrap, info.name, ok);
     if (ok) {
       if (info.pointerCount==1) {
         // store the wrapped pointer in an extra pointer and let ptr point to the extra pointer
         PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_ptrStorage, void*, object, ptr);
       } else if (info.pointerCount==0) {
         // store the wrapped pointer directly, since we are a reference
         ptr = object;
       }
     } else {
       // not matching, maybe a PyObject*?
       if (info.name == "PyObject" && info.pointerCount==1) {
         // handle low level PyObject directly
         PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_ptrStorage, void*, obj, ptr);
       }
     }
   } else if (info.pointerCount == 1) {
     // a pointer
     if (info.typeId == QMetaType::Char || info.typeId == QMetaType::UChar)
     {
       if (obj->ob_type == &PyBytes_Type) {
         // take direct reference to string data
         const char* data = PyBytes_AS_STRING(obj);
         PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_ptrStorage, void*, (void*)data, ptr);
       } else {
         // convert to string
         QString str = PyObjGetString(obj, strict, ok);
         if (ok) {
           QByteArray bytes;
           bytes = str.toUtf8();
           if (ok) {
             void* ptr2 = NULL;
             PythonQtValueStorage_ADD_VALUE_IF_NEEDED(NULL,global_variantStorage, QVariant, QVariant(bytes), ptr2);
             PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_ptrStorage, void*, (((QByteArray*)((QVariant*)ptr2)->constData())->data()), ptr);
           }
         }
       }
     } else if (info.typeId == QMetaType::QString) {
       // TODO: this is a special case for bad Qt APIs which take a QString*, like QtGui.QFileDialog.getSaveFileName
       // In general we would need to decide to either support * args for all basic types (ignoring the fact that the
       // result value is not useable in Python), or if all these APIs need to be wrapped manually/differently, like PyQt/PySide do.
       QString str = PyObjGetString(obj, strict, ok);
       if (ok) {
         void* ptr2 = NULL;
         PythonQtValueStorage_ADD_VALUE_IF_NEEDED(NULL,global_variantStorage, QVariant, QVariant(str), ptr2);
         PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_ptrStorage, void*, (void*)((QVariant*)ptr2)->constData(), ptr);
       }
     } else if (info.name == "PyObject") {
       // handle low level PyObject directly
       PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_ptrStorage, void*, obj, ptr);
     } else if (obj == Py_None) {
       // None is treated as a NULL ptr
       PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_ptrStorage, void*, NULL, ptr);
     } else {
       void* foreignWrapper = PythonQt::priv()->unwrapForeignWrapper(info.name, obj);
       if (foreignWrapper) {
         PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_ptrStorage, void*, foreignWrapper, ptr);
       } else {
         // if we are not strict, we try if we are passed a 0 integer
         if (!strict) {
           bool ok;
           int value = PyObjGetInt(obj, true, ok);
           if (ok && value==0) {
             // TODOXXX is this wise? or should it be expected from the programmer to use None?
             PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_ptrStorage, void*, NULL, ptr);
           }
         }
       }
     }
   } else if (info.pointerCount == 0) {
     // not a pointer
     switch (info.typeId) {
     case QMetaType::Char:
       {
         int val = PyObjGetInt(obj, strict, ok);
         if (ok && (val >= CHAR_MIN && val <= CHAR_MAX)) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, char, val, ptr);
         }
       }
       break;
     case QMetaType::UChar:
       {
         int val = PyObjGetInt(obj, strict, ok);
         if (ok && (val >= 0 && val <= UCHAR_MAX)) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, unsigned char, val, ptr);
         }
       }
       break;
     case QMetaType::Short:
       {
         int val = PyObjGetInt(obj, strict, ok);
         if (ok && (val >= SHRT_MIN && val <= SHRT_MAX)) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, short, val, ptr);
         }
       }
       break;
     case QMetaType::UShort:
       {
         int val = PyObjGetInt(obj, strict, ok);
         if (ok && (val >= 0 && val <= USHRT_MAX)) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, unsigned short, val, ptr);
         }
       }
       break;
     case QMetaType::Long:
       {
         qint64 val = PyObjGetLongLong(obj, strict, ok);
         if (ok && (val >= LONG_MIN && val <= LONG_MAX)) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, long, val, ptr);
         }
       }
       break;
     case QMetaType::ULong:
       {
         qint64 val = (unsigned long)PyObjGetLongLong(obj, strict, ok);
         if (ok && (val >= 0 && val <= ULONG_MAX)) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, unsigned long, val, ptr);
         }
       }
       break;
     case QMetaType::Bool:
       {
         bool val = PyObjGetBool(obj, strict, ok);
         if (ok) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, bool, val, ptr);
         }
       }
       break;
     case QMetaType::Int:
       {
         qint64 val = PyObjGetLongLong(obj, strict, ok);
         if (ok && (val >= INT_MIN && val <= INT_MAX)) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, int, val, ptr);
         }
       }
       break;
     case QMetaType::UInt:
       {
         quint64 val = PyObjGetLongLong(obj, strict, ok);
         if (ok && (val >= 0 && val <= UINT_MAX)) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, unsigned int, val, ptr);
         }
       }
       break;
     case QMetaType::QChar:
       {
         int val = PyObjGetInt(obj, strict, ok);
         if (ok && (val >= 0 && val <= USHRT_MAX)) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, unsigned short, val, ptr);
         }
       }
       break;
     case QMetaType::Float:
       {
         float val = (float)PyObjGetDouble(obj, strict, ok);
         if (ok) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, float, val, ptr);
         }
       }
       break;
     case QMetaType::Double:
       {
         double val = PyObjGetDouble(obj, strict, ok);
         if (ok) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, double, val, ptr);
         }
       }
       break;
     case QMetaType::LongLong:
       {
         qint64 val = PyObjGetLongLong(obj, strict, ok);
         if (ok) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, qint64, val, ptr);
         }
       }
       break;
     case QMetaType::ULongLong:
       {
         quint64 val = PyObjGetULongLong(obj, strict, ok);
         if (ok) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, quint64, val, ptr);
         }
       }
       break;
     case QMetaType::QByteArray:
       {
         QByteArray bytes = PyObjGetBytes(obj, strict, ok);
#ifdef PY3K
         if (!ok && !strict) {
           // since Qt uses QByteArray in many places for identifier strings,
           // we need to allow implicit conversion from unicode as well:
           bytes = PyObjGetString(obj, strict, ok).toUtf8();
         }
#endif
         if (ok) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_variantStorage, QVariant, QVariant(bytes), ptr);
           ptr = (void*)((QVariant*)ptr)->constData();
         }
       }
       break;
     case QMetaType::QString:
       {
         QString str = PyObjGetString(obj, strict, ok);
         if (ok) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_variantStorage, QVariant, QVariant(str), ptr);
           ptr = (void*)((QVariant*)ptr)->constData();
         }
       }
       break;
     case QMetaType::QStringList:
       {
         QStringList l = PyObjToStringList(obj, strict, ok);
         if (ok) {
           PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_variantStorage, QVariant, QVariant(l), ptr);
           ptr = (void*)((QVariant*)ptr)->constData();
         }
       }
       break;

     case PythonQtMethodInfo::Variant:
       {
         QVariant v = PyObjToQVariant(obj);
         // the only case where conversion can fail it None and we want to pass that to, e.g. setProperty(),
         // so we do not check v.isValid() here
         PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_variantStorage, QVariant, v, ptr);
       }
       break;
       default:
       {
         // check for enum case
         if (info.enumWrapper) {
           unsigned int val;
           ok = false;
           if ((PyObject*)obj->ob_type == info.enumWrapper) {
             // we have a exact enum type match:
             val = PyLong_AsLong(obj);
             ok = true;
           } else if (!strict) {
             // we try to get any integer, when not being strict. If we are strict, integers are not wanted because
             // we want an integer overload to be taken first!
             val = (unsigned int)PyObjGetLongLong(obj, false, ok);
           }
           if (ok) {
             PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_valueStorage, unsigned int, val, ptr);
             return ptr;
           } else {
             return NULL;
           }
         }

         if (info.typeId == PythonQtMethodInfo::Unknown || info.typeId >= QMetaType::User) {
           // check for QList<AnyPtr*> case, where we will use a QList<void*> QVariant
           if (info.isQList && (info.innerNamePointerCount == 1)) {
             static int id = QMetaType::type("QList<void*>");
             if (!alreadyAllocatedCPPObject) {
               PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject, global_variantStorage, QVariant, QVariant::Type(id), ptr);
               ptr = (void*)((QVariant*)ptr)->constData();
             } else {
               ptr = alreadyAllocatedCPPObject;
             }
             ok = ConvertPythonListToQListOfPointerType(obj, (QList<void*>*)ptr, info.innerName, strict);
             if (ok) {
               return ptr;
             } else {
               return NULL;
             }
           }
         }

         // We only do this for registered type > QMetaType::User for performance reasons.
         if (info.typeId >= QMetaType::User) {
           // Maybe we have a special converter that is registered for that type:
           PythonQtConvertPythonToMetaTypeCB* converter = _pythonToMetaTypeConverters.value(info.typeId);
           if (converter) {
             if (!alreadyAllocatedCPPObject) {
               // create a new empty variant of concrete type:
               PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_variantStorage, QVariant, QVariant::Type(info.typeId), ptr);
               ptr = (void*)((QVariant*)ptr)->constData();
             } else {
               ptr = alreadyAllocatedCPPObject;
             }
             // now call the converter, passing the internal object of the variant
             ok = (*converter)(obj, ptr, info.typeId, strict);
             if (ok) {
               return ptr;
             } else {
               return NULL;
             }
           }
         }
         // if no type id is available, conversion to a QVariant makes no sense/is not possible
         if (info.typeId != PythonQtMethodInfo::Unknown) {
           // for all other types, we use the same qvariant conversion and pass out the constData of the variant:
           QVariant v = PyObjToQVariant(obj, info.typeId);
           if (v.isValid()) {
             PythonQtValueStorage_ADD_VALUE_IF_NEEDED(alreadyAllocatedCPPObject,global_variantStorage, QVariant, v, ptr);
             ptr = (void*)((QVariant*)ptr)->constData();
           }
         }
       }
    }
  }
  return ptr;
}


QStringList PythonQtConv::PyObjToStringList(PyObject* val, bool strict, bool& ok) {
  QStringList v;
  ok = false;
  // if we are strict, we do not want to convert a string to a stringlist
  // (strings in python are detected to be sequences)
  if (strict &&
    (val->ob_type == &PyBytes_Type ||
    PyUnicode_Check(val))) {
    return v;
  }
  if (PySequence_Check(val)) {
    int count = PySequence_Size(val);
    if (count >= 0) {
      for (int i = 0;i<count;i++) {
        PyObject* value = PySequence_GetItem(val,i);
        v.append(PyObjGetString(value,false,ok));
      }
      ok = true;
    }
  }
  return v;
}

QString PythonQtConv::PyObjGetRepresentation(PyObject* val)
{
  QString r;
  PyObject* str =  PyObject_Repr(val);
  if (str) {
    #ifdef PY3K
      r = PyObjGetString(str);
    #else
      r = QString(PyString_AS_STRING(str));
    #endif
    Py_DECREF(str);
  }
  return r;
}

QString PythonQtConv::PyObjGetString(PyObject* val, bool strict, bool& ok) {
  QString r;
  ok = true;
  if (val->ob_type == &PyBytes_Type) {
    r = QString(PyBytes_AS_STRING(val));
  } else if (PyUnicode_Check(val)) {
#ifdef PY3K
    r = QString::fromUtf8(PyUnicode_AsUTF8(val));
#else
    PyObject *ptmp = PyUnicode_AsUTF8String(val);
    if(ptmp) {
      r = QString::fromUtf8(PyString_AS_STRING(ptmp));
      Py_DECREF(ptmp);
    }
#endif
  } else if (!strict) {
    PyObject* str =  PyObject_Str(val);
    if (str) {
#ifdef PY3K
      r = QString::fromUtf8(PyUnicode_AsUTF8(str));
#else
      r = QString(PyString_AS_STRING(str));
#endif
      Py_DECREF(str);
    } else {
      ok = false;
    }
  } else {
    ok = false;
  }
  return r;
}

QByteArray PythonQtConv::PyObjGetBytes(PyObject* val, bool /*strict*/, bool& ok) {
  // TODO: support buffer objects in general
  QByteArray r;
  ok = true;
  if (PyBytes_Check(val)) {
    r = QByteArray(PyBytes_AS_STRING(val), PyBytes_GET_SIZE(val));
  } else {
    ok = false;
  }
  return r;
}

bool PythonQtConv::PyObjGetBool(PyObject* val, bool strict, bool &ok) {
  bool d = false;
  ok = false;
  if (val == Py_False) {
    d = false;
    ok = true;
  } else if (val == Py_True) {
    d = true;
    ok = true;
  } else if (!strict) {
    int result = PyObject_IsTrue(val);
    d = (result == 1);
    // the result is -1 if an error occurred, handle this:
    ok = (result != -1);
  }
  return d;
}

int PythonQtConv::PyObjGetInt(PyObject* val, bool strict, bool &ok) {
  int d = 0;
  ok = true;
  if (val->ob_type == &PyInt_Type) {
    d = PyInt_AS_LONG(val);
  } else if (!strict) {
    if (PyObject_TypeCheck(val, &PyInt_Type)) {
      // support for derived int classes, e.g. for our enums
      d = PyInt_AS_LONG(val);
    } else if (val->ob_type == &PyFloat_Type) {
      d = floor(PyFloat_AS_DOUBLE(val));
    } else if (val->ob_type == &PyLong_Type) {
      // handle error on overflow!
      d = PyLong_AsLong(val);
    } else if (val == Py_False) {
      d = 0;
    } else if (val == Py_True) {
      d = 1;
    } else {
      PyErr_Clear();
      // PyInt_AsLong will try conversion to an int if the object is not an int:
      d = PyLong_AsLong(val);
      if (PyErr_Occurred()) {
        ok = false;
        PyErr_Clear();
      }
    }
  } else {
    ok = false;
  }
  return d;
}

qint64 PythonQtConv::PyObjGetLongLong(PyObject* val, bool strict, bool &ok) {
  qint64 d = 0;
  ok = true;
#ifndef PY3K
  if (val->ob_type == &PyInt_Type) {
    d = PyInt_AS_LONG(val);
  } else
#endif
  if (val->ob_type == &PyLong_Type) {
    d = PyLong_AsLongLong(val);
  } else if (!strict) {
    if (PyObject_TypeCheck(val, &PyInt_Type)) {
      // support for derived int classes, e.g. for our enums
      d = PyInt_AS_LONG(val);
    } else if (val->ob_type == &PyFloat_Type) {
      d = floor(PyFloat_AS_DOUBLE(val));
    } else if (val == Py_False) {
      d = 0;
    } else if (val == Py_True) {
      d = 1;
    } else {
      PyErr_Clear();
      // PyLong_AsLongLong will try conversion to an int if the object is not an int:
      d = PyLong_AsLongLong(val);
      if (PyErr_Occurred()) {
        ok = false;
        PyErr_Clear();
      }
    }
  } else {
    ok = false;
  }
  return d;
}

quint64 PythonQtConv::PyObjGetULongLong(PyObject* val, bool strict, bool &ok) {
  quint64 d = 0;
  ok = true;
#ifndef PY3K
  if (Py_TYPE(val) == &PyInt_Type) {
    d = PyInt_AS_LONG(val);
  } else
#endif
  if (Py_TYPE(val) == &PyLong_Type) {
    d = PyLong_AsLongLong(val);
  } else if (!strict) {
    if (PyObject_TypeCheck(val, &PyInt_Type)) {
      // support for derived int classes, e.g. for our enums
      d = PyInt_AS_LONG(val);
    } else if (val->ob_type == &PyFloat_Type) {
      d = floor(PyFloat_AS_DOUBLE(val));
    } else if (val == Py_False) {
      d = 0;
    } else if (val == Py_True) {
      d = 1;
    } else {
      PyErr_Clear();
      // PyLong_AsLongLong will try conversion to an int if the object is not an int:
      d = PyLong_AsLongLong(val);
      if (PyErr_Occurred()) {
        PyErr_Clear();
        ok = false;
      }
    }
  } else {
    ok = false;
  }
  return d;
}

double PythonQtConv::PyObjGetDouble(PyObject* val, bool strict, bool &ok) {
  double d = 0;
  ok = true;
  if (val->ob_type == &PyFloat_Type) {
    d = PyFloat_AS_DOUBLE(val);
  } else if (!strict) {
#ifndef PY3K
    if (PyObject_TypeCheck(val, &PyInt_Type)) {
      d = PyInt_AS_LONG(val);
    } else
#endif
    if (PyLong_Check(val)) {
      d = PyLong_AsLong(val);
    } else if (val == Py_False) {
      d = 0;
    } else if (val == Py_True) {
      d = 1;
    } else {
      PyErr_Clear();
      // PyFloat_AsDouble will try conversion to a double if the object is not a float:
      d = PyFloat_AsDouble(val);
      if (PyErr_Occurred()) {
        PyErr_Clear();
        ok = false;
      }
    }
  } else {
    ok = false;
  }
  return d;
}

template <typename Map> 
void PythonQtConv::pythonToMapVariant(PyObject* val, QVariant& result)
{
  if (PyMapping_Check(val)) {
    Map map;
    PyObject* items = PyMapping_Items(val);
    if (items) {
      int count = PyList_Size(items);
      PyObject* value;
      PyObject* key;
      PyObject* tuple;
      for (int i = 0;i<count;i++) {
        tuple = PyList_GetItem(items,i);
        key = PyTuple_GetItem(tuple, 0);
        value = PyTuple_GetItem(tuple, 1);
        map.insert(PyObjGetString(key), PyObjToQVariant(value,-1));
      }
      Py_DECREF(items);
      result = map;
    }
  }
}


QVariant PythonQtConv::PyObjToQVariant(PyObject* val, int type)
{
  QVariant v;
  bool ok = true;

  if (type == -1 
#if QT_VERSION >= 0x040800
    || type == QMetaType::QVariant
#endif
    ) {
    // no special type requested
    if (PyBytes_Check(val) || PyUnicode_Check(val)) {
      // NOTE: for compatibility reasons between Python 2/3 we don't use ByteArray for PyBytes_Type
      type = QVariant::String;
    } else if (val == Py_False || val == Py_True) {
      type = QVariant::Bool;
#ifndef PY3K
    } else if (PyObject_TypeCheck(val, &PyInt_Type)) {
      type = QVariant::Int;
#endif
    } else if (PyLong_Check(val)) {
      type = QVariant::LongLong;
    } else if (PyFloat_Check(val)) {
      type = QVariant::Double;
    } else if (PyObject_TypeCheck(val, &PythonQtInstanceWrapper_Type)) {
      PythonQtInstanceWrapper* wrap = (PythonQtInstanceWrapper*)val;
      // c++ wrapper, check if the class names of the c++ objects match
      if (wrap->classInfo()->isCPPWrapper()) {
        if (wrap->classInfo()->metaTypeId()>0) {
          // construct a new variant from the C++ object if it has a meta type (this will COPY the object!)
          v = QVariant(wrap->classInfo()->metaTypeId(), wrap->_wrappedPtr);
        } else {
          // TODOXXX we could as well check if there is a registered meta type for "classname*", so that we may pass
          // the pointer here...
          // is this worth anything? we loose the knowledge of the cpp object type
          v = qVariantFromValue(wrap->_wrappedPtr);
        }
      } else {
        // this gives us a QObject pointer
        QObject* myObject = wrap->_obj;
        v = qVariantFromValue(myObject);
      }
      return v;
    } else if (PyDict_Check(val)) {
      type = QVariant::Map;
    } else if (PyList_Check(val) || PyTuple_Check(val) || PySequence_Check(val)) {
      type = QVariant::List;
    } else if (val == Py_None) {
      // none is invalid
      type = QVariant::Invalid;
    } else {
      // this used to be:
      // type = QVariant::String;
      // but now we want to transport the Python Objects directly:
      PythonQtObjectPtr o(val);
      v = qVariantFromValue(o);
      return v;
    }
  }
  // special type request:
  switch (type) {
  case QVariant::Invalid:
    return v;
    break;
  case QVariant::Int:
    {
      int d = PyObjGetInt(val, false, ok);
      if (ok) return QVariant(d);
    }
    break;
  case QVariant::UInt:
    {
      int d = PyObjGetInt(val, false,ok);
      if (ok) v = QVariant((unsigned int)d);
    }
    break;
  case QVariant::Bool:
    {
      int d = PyObjGetBool(val,false,ok);
      if (ok) v =  QVariant((bool)(d!=0));
    }
    break;
  case QVariant::Double:
    {
      double d = PyObjGetDouble(val,false,ok);
      if (ok) v =  QVariant(d);
      break;
    }
  case QMetaType::Float:
    {
      float d = (float) PyObjGetDouble(val,false,ok);
      if (ok) v =  qVariantFromValue(d);
      break;
    }
  case QMetaType::Long:
    {
      long d = (long) PyObjGetLongLong(val,false,ok);
      if (ok) v =  qVariantFromValue(d);
      break;
    }
  case QMetaType::ULong:
    {
      unsigned long d = (unsigned long) PyObjGetLongLong(val,false,ok);
      if (ok) v =  qVariantFromValue(d);
      break;
    }
  case QMetaType::LongLong:
    {
      qint64 d = PyObjGetLongLong(val, false, ok);
      if (ok) v =  qVariantFromValue(d);
    }
    break;
  case QMetaType::ULongLong:
    {
      quint64 d = PyObjGetULongLong(val, false, ok);
      if (ok) v =  qVariantFromValue(d);
    }
    break;
  case QMetaType::Short:
    {
      short d = (short) PyObjGetInt(val,false,ok);
      if (ok) v =  qVariantFromValue(d);
      break;
    }
  case QMetaType::UShort:
    {
      unsigned short d = (unsigned short) PyObjGetInt(val,false,ok);
      if (ok) v =  qVariantFromValue(d);
      break;
    }
  case QMetaType::Char:
    {
      char d = (char) PyObjGetInt(val,false,ok);
      if (ok) v =  qVariantFromValue(d);
      break;
    }
  case QMetaType::UChar:
    {
      unsigned char d = (unsigned char) PyObjGetInt(val,false,ok);
      if (ok) v =  qVariantFromValue(d);
      break;
    }

  case QVariant::ByteArray:
  case QVariant::String:
    {
      bool ok;
      v = QVariant(PyObjGetString(val, false, ok));
    }
    break;

  case QVariant::Map:
    pythonToMapVariant<QVariantMap>(val, v);
    break;
  case QVariant::Hash:
    pythonToMapVariant<QVariantHash>(val, v);
    break;
  case QVariant::List:
    if (PySequence_Check(val)) {
      int count = PySequence_Size(val);
      if (count >= 0) {
        // only get items if size is valid (>= 0)
        QVariantList list;
        PyObject* value;
        for (int i = 0;i<count;i++) {
          value = PySequence_GetItem(val,i);
          list.append(PyObjToQVariant(value, -1));
        }
        v = list;
      }
    }
    break;
  case QVariant::StringList:
    {
      bool ok;
      QStringList l = PyObjToStringList(val, false, ok);
      if (ok) {
        v = l;
      }
    }
    break;

  default:
    if (PyObject_TypeCheck(val, &PythonQtInstanceWrapper_Type)) {
      PythonQtInstanceWrapper* wrap = (PythonQtInstanceWrapper*)val;
      if (wrap->classInfo()->isCPPWrapper() && wrap->classInfo()->metaTypeId() == type) {
        // construct a new variant from the C++ object if it has the same meta type
        v = QVariant(type, wrap->_wrappedPtr);
      } else {
        // Try to convert the object to a QVariant based on the typeName
        bool ok;
        bool isPtr = false;
        QByteArray typeName = QMetaType::typeName(type);
        if (typeName.endsWith("*")) {
          isPtr = true;
          typeName.truncate(typeName.length() - 1);
        }
        void* object = castWrapperTo(wrap, typeName, ok);
        if (ok) {
          if (isPtr) {
            v = QVariant(type, &object);
          }
          else {
            v = QVariant(type, object);
          }
        }
      }
    } else if (type >= QVariant::UserType) {
      // not an instance wrapper, but there might be other converters 
      // Maybe we have a special converter that is registered for that type:
      PythonQtConvertPythonToMetaTypeCB* converter = _pythonToMetaTypeConverters.value(type);
      if (converter) {
        // allocate a default object of the needed type:
        v = QVariant(type, (const void*)NULL);
        // now call the converter, passing the internal object of the variant
        ok = (*converter)(val, (void*)v.constData(), type, true);
        if (!ok) {
          v = QVariant();
        }
      } else {
        // try QList<AnyObject*>...
        const PythonQtMethodInfo::ParameterInfo& info = PythonQtMethodInfo::getParameterInfoForMetaType(type);
        if (info.isQList && (info.innerNamePointerCount == 1)) {
          // allocate a default object of the needed type:
          v = QVariant(type, (const void*)NULL);
          ok = ConvertPythonListToQListOfPointerType(val, (QList<void*>*)v.constData(), info.innerName, true);
          if (!ok) {
            v = QVariant();
          }
        }
      }
    }
  }
  return v;
}

PyObject* PythonQtConv::QStringToPyObject(const QString& str)
{
  if (str.isNull()) {
    return PyString_FromString("");
  } else {
    return PyUnicode_DecodeUTF16((const char*)str.utf16(), str.length()*2, NULL, NULL);
  }
}

PyObject* PythonQtConv::QStringListToPyObject(const QStringList& list)
{
  PyObject* result = PyTuple_New(list.count());
  int i = 0;
  QString str;
  Q_FOREACH (str, list) {
    PyTuple_SET_ITEM(result, i, PythonQtConv::QStringToPyObject(str));
    i++;
  }
  // why is the error state bad after this?
  PyErr_Clear();
  return result;
}

PyObject* PythonQtConv::QStringListToPyList(const QStringList& list)
{
  PyObject* result = PyList_New(list.count());
  int i = 0;
  for (QStringList::ConstIterator it = list.begin(); it!=list.end(); ++it) {
    PyList_SET_ITEM(result, i, PythonQtConv::QStringToPyObject(*it));
    i++;
  }
  return result;
}

PyObject* PythonQtConv::QVariantToPyObject(const QVariant& v)
{
  if (!v.isValid()) {
    Py_INCREF(Py_None);
    return Py_None;
  }
  PyObject* obj = NULL;
  if (v.userType() >= QMetaType::User && !PythonQt::priv()->isPythonQtObjectPtrMetaId(v.userType())) {
    // try the slower way, which supports more conversions, e.g. QList<QObject*>
    const PythonQtMethodInfo::ParameterInfo& info = PythonQtMethodInfo::getParameterInfoForMetaType(v.userType());
    obj = ConvertQtValueToPython(info, v.constData());
  } else {
    // try the quick way to convert it, since it is a built-in type:
    obj = ConvertQtValueToPythonInternal(v.userType(), (void*)v.constData());
  }
  return obj;
}

template <typename Map>
PyObject* PythonQtConv::mapToPython (const Map& m)
{
  PyObject* result = PyDict_New();
  typename Map::const_iterator t = m.constBegin();
  PyObject* key;
  PyObject* val;
  for (;t!=m.constEnd();t++) {
    key = QStringToPyObject(t.key());
    val = QVariantToPyObject(t.value());
    PyDict_SetItem(result, key, val);
    Py_DECREF(key);
    Py_DECREF(val);
  }
  return result;
}

PyObject* PythonQtConv::QVariantMapToPyObject(const QVariantMap& m) {
  return mapToPython<QVariantMap>(m);
}

PyObject* PythonQtConv::QVariantHashToPyObject(const QVariantHash& m) {
  return mapToPython<QVariantHash>(m);
}

PyObject* PythonQtConv::QVariantListToPyObject(const QVariantList& l) {
  PyObject* result = PyTuple_New(l.count());
  int i = 0;
  QVariant v;
  Q_FOREACH (v, l) {
    PyTuple_SET_ITEM(result, i, PythonQtConv::QVariantToPyObject(v));
    i++;
  }
  // why is the error state bad after this?
  PyErr_Clear();
  return result;
}

PyObject* PythonQtConv::ConvertQListOfPointerTypeToPythonList(QList<void*>* list, const QByteArray& typeName)
{
  PyObject* result = PyTuple_New(list->count());
  int i = 0;
  Q_FOREACH (void* value, *list) {
    PyTuple_SET_ITEM(result, i, PythonQt::priv()->wrapPtr(value, typeName));
    i++;
  }
  return result;
}

bool PythonQtConv::ConvertPythonListToQListOfPointerType(PyObject* obj, QList<void*>* list, const QByteArray& type, bool /*strict*/)
{
  bool result = false;
  if (PySequence_Check(obj)) {
    int count = PySequence_Size(obj);
    if (count >= 0) {
      result = true;
      PyObject* value;
      for (int i = 0;i<count;i++) {
        value = PySequence_GetItem(obj,i);
        if (PyObject_TypeCheck(value, &PythonQtInstanceWrapper_Type)) {
          PythonQtInstanceWrapper* wrap = (PythonQtInstanceWrapper*)value;
          bool ok;
          void* object = castWrapperTo(wrap, type, ok);
          if (ok) {
            list->append(object);
          } else {
            result = false;
            break;
          }
        }
      }
    }
  }
  return result;
}


QString PythonQtConv::CPPObjectToString(int type, const void* data) {
  QString r;
  switch (type) {
    case QVariant::Size: {
      const QSize* s = static_cast<const QSize*>(data);
      r = QString::number(s->width()) + ", " + QString::number(s->height());
      }
      break;
    case QVariant::SizeF: {
      const QSizeF* s = static_cast<const QSizeF*>(data);
      r = QString::number(s->width()) + ", " + QString::number(s->height());
      }
      break;
    case QVariant::Point: {
      const QPoint* s = static_cast<const QPoint*>(data);
      r = QString::number(s->x()) + ", " + QString::number(s->y());
      }
      break;
    case QVariant::PointF: {
      const QPointF* s = static_cast<const QPointF*>(data);
      r = QString::number(s->x()) + ", " + QString::number(s->y());
      }
      break;
    case QVariant::Rect: {
      const QRect* s = static_cast<const QRect*>(data);
      r = QString::number(s->x()) + ", " + QString::number(s->y());
      r += ", " + QString::number(s->width()) + ", " + QString::number(s->height());
      }
      break;
    case QVariant::RectF: {
      const QRectF* s = static_cast<const QRectF*>(data);
      r = QString::number(s->x()) + ", " + QString::number(s->y());
      r += ", " + QString::number(s->width()) + ", " + QString::number(s->height());
      }
      break;
    case QVariant::Date: {
      const QDate* s = static_cast<const QDate*>(data);
      r = s->toString(Qt::ISODate);
      }
      break;
    case QVariant::DateTime: {
      const QDateTime* s = static_cast<const QDateTime*>(data);
      r = s->toString(Qt::ISODate);
      }
      break;
    case QVariant::Time: {
      const QTime* s = static_cast<const QTime*>(data);
      r = s->toString(Qt::ISODate);
    }
      break;
    case QVariant::Pixmap:
    {
      const QPixmap* s = static_cast<const QPixmap*>(data);
      r = QString("Pixmap ") + QString::number(s->width()) + ", " + QString::number(s->height());
    }
      break;
    case QVariant::Image:
    {
      const QImage* s = static_cast<const QImage*>(data);
      r = QString("Image ") + QString::number(s->width()) + ", " + QString::number(s->height());
    }
      break;
    case QVariant::Url:
      {
        const QUrl* s = static_cast<const QUrl*>(data);
        r = s->toString();
      }
      break;
      //TODO: add more printing for other variant types
    default:
      // this creates a copy, but that should not be expensive for typical simple variants
      // (but we do not want to do this for our won user types!
      if (type>0 && type < (int)QVariant::UserType) {
        QVariant v(type, data);
        r = v.toString();
      }
  }
  return r;
}

PyObject* PythonQtConv::createCopyFromMetaType( int type, const void* data )
{
  // if the type is known, we can construct it via QMetaType::construct
#if( QT_VERSION >= QT_VERSION_CHECK(5,0,0) )
  void* newCPPObject = QMetaType::create(type, data);
#else
  void* newCPPObject = QMetaType::construct(type, data);
#endif
  // XXX this could be optimized by using metatypeid directly
  PythonQtInstanceWrapper* wrap = (PythonQtInstanceWrapper*)PythonQt::priv()->wrapPtr(newCPPObject, QMetaType::typeName(type));
  wrap->_ownedByPythonQt = true;
  wrap->_useQMetaTypeDestroy = true;
  return (PyObject*)wrap;
}