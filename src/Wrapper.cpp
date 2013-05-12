#include "Wrapper.h"

#include <stdlib.h>

#include <vector>

#include <boost/preprocessor.hpp>
#include <boost/python/raw_function.hpp>

#include <descrobject.h>
#include <datetime.h>

#include "V8Internal.h"

#include "Context.h"
#include "Utils.h"

#define TERMINATE_EXECUTION_CHECK(returnValue) \
  if(v8::V8::IsExecutionTerminating()) { \
    ::PyErr_Clear(); \
    ::PyErr_SetString(PyExc_RuntimeError, "execution is terminating"); \
    return returnValue; \
  }

#define CHECK_V8_CONTEXT() \
  if (!v8i::Isolate::Current()->context()) { \
    throw CJavascriptException("Javascript object out of context", PyExc_UnboundLocalError); \
  }

std::ostream& operator <<(std::ostream& os, const CJavascriptObject& obj)
{ 
  obj.Dump(os);

  return os;
}

void CWrapper::Expose(void)
{
  PyDateTime_IMPORT;

  py::class_<CJavascriptObject, boost::noncopyable>("JSObject", py::no_init)
    .add_property("__js__", &CJavascriptObject::Native)

    .def("__getattr__", &CJavascriptObject::GetAttr)
    .def("__setattr__", &CJavascriptObject::SetAttr)
    .def("__delattr__", &CJavascriptObject::DelAttr)    

    .def("__hash__", &CJavascriptObject::GetIdentityHash)
    .def("clone", &CJavascriptObject::Clone, "Clone the object.")

  #if PY_MAJOR_VERSION < 3
    .add_property("__members__", &CJavascriptObject::GetAttrList)
  #else
    .def("__dir__", &CJavascriptObject::GetAttrList)
  #endif
  
    // Emulating dict object
    .def("keys", &CJavascriptObject::GetAttrList, "Get a list of an object's attributes.")

    .def("__getitem__", &CJavascriptObject::GetAttr)
    .def("__setitem__", &CJavascriptObject::SetAttr)
    .def("__delitem__", &CJavascriptObject::DelAttr)

    .def("__contains__", &CJavascriptObject::Contains)

    .def(int_(py::self))
    .def(float_(py::self))
    .def(str(py::self))

    .def("__nonzero__", &CJavascriptObject::operator bool)
    .def("__eq__", &CJavascriptObject::Equals)
    .def("__ne__", &CJavascriptObject::Unequals)

	  .def("create", &CJavascriptFunction::CreateWithArgs, 
         (py::arg("constructor"), 
          py::arg("arguments") = py::tuple(),
          py::arg("propertiesObject") = py::dict()),
         "Creates a new object with the specified prototype object and properties.")
	  .staticmethod("create")
    ;

  py::class_<CJavascriptArray, py::bases<CJavascriptObject>, boost::noncopyable>("JSArray", py::no_init)    
    .def(py::init<py::object>())    

    .def("__len__", &CJavascriptArray::Length)

    .def("__getitem__", &CJavascriptArray::GetItem)
    .def("__setitem__", &CJavascriptArray::SetItem)
    .def("__delitem__", &CJavascriptArray::DelItem)

    .def("__iter__", py::range(&CJavascriptArray::begin, &CJavascriptArray::end))

    .def("__contains__", &CJavascriptArray::Contains)
    ;

  py::class_<CJavascriptFunction, py::bases<CJavascriptObject>, boost::noncopyable>("JSFunction", py::no_init)    
    .def("__call__", py::raw_function(&CJavascriptFunction::CallWithArgs))

    .def("invoke", &CJavascriptFunction::Apply, 
         (py::arg("self"), 
          py::arg("args") = py::list(), 
          py::arg("kwds") = py::dict()), 
          "Performs a function call using the parameters.")
    .def("invoke", &CJavascriptFunction::Invoke, 
          (py::arg("args") = py::list(), 
           py::arg("kwds") = py::dict()), 
          "Performs a binding method call using the parameters.")

    .def("setName", &CJavascriptFunction::SetName)

    .add_property("name", &CJavascriptFunction::GetName, &CJavascriptFunction::SetName, "The name of function")
    .add_property("owner", &CJavascriptFunction::GetOwner)

    .add_property("linenum", &CJavascriptFunction::GetLineNumber, "The line number of function in the script")
    .add_property("colnum", &CJavascriptFunction::GetColumnNumber, "The column number of function in the script")
    .add_property("resname", &CJavascriptFunction::GetResourceName, "The resource name of script")
    .add_property("inferredname", &CJavascriptFunction::GetInferredName, "Name inferred from variable or property assignment of this function")
    .add_property("lineoff", &CJavascriptFunction::GetLineOffset, "The line offset of function in the script")
    .add_property("coloff", &CJavascriptFunction::GetColumnOffset, "The column offset of function in the script")
    ;

  py::objects::class_value_wrapper<boost::shared_ptr<CJavascriptObject>, 
    py::objects::make_ptr_instance<CJavascriptObject, 
    py::objects::pointer_holder<boost::shared_ptr<CJavascriptObject>,CJavascriptObject> > >();
}

void CPythonObject::ThrowIf(void)
{  
  CPythonGIL python_gil;
  
  assert(PyErr_OCCURRED());

  v8::HandleScope handle_scope;

  PyObject *exc, *val, *trb;  

  ::PyErr_Fetch(&exc, &val, &trb);
  ::PyErr_NormalizeException(&exc, &val, &trb);

  py::object type(py::handle<>(py::allow_null(exc))),
             value(py::handle<>(py::allow_null(val)));

  if (trb) py::decref(trb);
  
  std::string msg;

  if (::PyObject_HasAttrString(value.ptr(), "args"))
  {
    py::object args = value.attr("args");

    if (PyTuple_Check(args.ptr()))
    {
      for (Py_ssize_t i=0; i<PyTuple_GET_SIZE(args.ptr()); i++)
      {
        py::extract<const std::string> extractor(args[i]);

        if (extractor.check()) msg += extractor();
      }
    }
  }
  else if (::PyObject_HasAttrString(value.ptr(), "message"))
  {
    py::extract<const std::string> extractor(value.attr("message"));

    if (extractor.check()) msg = extractor();
  }
  else if (val)
  {
    if (PyBytes_CheckExact(val))
    {
      msg = PyBytes_AS_STRING(val);
    }
    else if (PyTuple_CheckExact(val))
    {
      for (int i=0; i<PyTuple_GET_SIZE(val); i++)
      {
        PyObject *item = PyTuple_GET_ITEM(val, i);

        if (item && PyBytes_CheckExact(item))
        {
          msg = PyBytes_AS_STRING(item);
          break;
        }
      }
    }
  }

  v8::Handle<v8::Value> error;

  if (::PyErr_GivenExceptionMatches(type.ptr(), ::PyExc_IndexError))
  {
    error = v8::Exception::RangeError(v8::String::New(msg.c_str(), msg.size()));
  }
  else if (::PyErr_GivenExceptionMatches(type.ptr(), ::PyExc_AttributeError))
  {
    error = v8::Exception::ReferenceError(v8::String::New(msg.c_str(), msg.size()));
  }
  else if (::PyErr_GivenExceptionMatches(type.ptr(), ::PyExc_SyntaxError))
  {
    error = v8::Exception::SyntaxError(v8::String::New(msg.c_str(), msg.size()));
  }
  else if (::PyErr_GivenExceptionMatches(type.ptr(), ::PyExc_TypeError))
  {
    error = v8::Exception::TypeError(v8::String::New(msg.c_str(), msg.size()));
  }
  else
  {
    error = v8::Exception::Error(v8::String::New(msg.c_str(), msg.size()));
  }

  if (error->IsObject())
  {
    // FIXME How to trace the lifecycle of exception? and when to delete those object in the hidden value?

  #ifdef SUPPORT_TRACE_EXCEPTION_LIFECYCLE
    error->ToObject()->SetHiddenValue(v8::String::NewSymbol("exc_type"), v8::External::New(ObjectTracer::Trace(error, new py::object(type)).Object()));
    error->ToObject()->SetHiddenValue(v8::String::NewSymbol("exc_value"), v8::External::New(ObjectTracer::Trace(error, new py::object(value)).Object()));
  #else
    error->ToObject()->SetHiddenValue(v8::String::NewSymbol("exc_type"), v8::External::New(new py::object(type)));
    error->ToObject()->SetHiddenValue(v8::String::NewSymbol("exc_value"), v8::External::New(new py::object(value)));
  #endif
  }

  v8::ThrowException(error);
}

#define TRY_HANDLE_EXCEPTION() BEGIN_HANDLE_PYTHON_EXCEPTION {
#define END_HANDLE_EXCEPTION(result) } END_HANDLE_PYTHON_EXCEPTION \
  return result;

v8::Handle<v8::Value> CPythonObject::NamedGetter(
  v8::Local<v8::String> prop, const v8::AccessorInfo& info)
{
  TRY_HANDLE_EXCEPTION()
  
  CPythonGIL python_gil;

  TERMINATE_EXECUTION_CHECK(v8::Undefined())

  py::object obj = CJavascriptObject::Wrap(info.Holder());  

  v8::String::Utf8Value name(prop);

  if (PyGen_Check(obj.ptr())) return v8::Undefined();

  PyObject *value = ::PyObject_GetAttrString(obj.ptr(), *name);

  if (!value)
  {
    if (PyErr_OCCURRED())
    {
      if (::PyErr_ExceptionMatches(::PyExc_AttributeError))
      {
        ::PyErr_Clear();
      }
      else
      {
        py::throw_error_already_set();
      }
    }

    if (::PyMapping_Check(obj.ptr()) && 
        ::PyMapping_HasKeyString(obj.ptr(), *name)) 
    {      
      py::object result(py::handle<>(::PyMapping_GetItemString(obj.ptr(), *name)));

      if (result.ptr() != Py_None) return Wrap(result);
    }

    return v8::Handle<v8::Value>();
  }

  py::object attr = py::object(py::handle<>(value));

#ifdef SUPPORT_PROPERTY
  if (PyObject_TypeCheck(attr.ptr(), &::PyProperty_Type))
  {
    py::object getter = attr.attr("fget");

    if (getter.ptr() == Py_None)
      throw CJavascriptException("unreadable attribute", ::PyExc_AttributeError);    
    
    attr = getter();
  }
#endif

  return Wrap(attr);

  END_HANDLE_EXCEPTION(v8::Undefined())
}

v8::Handle<v8::Value> CPythonObject::NamedSetter(
  v8::Local<v8::String> prop, v8::Local<v8::Value> value, const v8::AccessorInfo& info)
{
  TRY_HANDLE_EXCEPTION()

  v8::HandleScope handle_scope;
  CPythonGIL python_gil;

  TERMINATE_EXECUTION_CHECK(v8::Undefined())

  py::object obj = CJavascriptObject::Wrap(info.Holder());

  v8::String::Utf8Value name(prop);
  py::object newval = CJavascriptObject::Wrap(value);

  bool found = 1 == ::PyObject_HasAttrString(obj.ptr(), *name);

  if (::PyObject_HasAttrString(obj.ptr(), "__watchpoints__"))
  {
    py::dict watchpoints(obj.attr("__watchpoints__"));
    py::str propname(*name, name.length());

    if (watchpoints.has_key(propname))
    {
      py::object watchhandler = watchpoints.get(propname);      

      newval = watchhandler(propname, found ? obj.attr(propname) : py::object(), newval);
    }
  }

  if (!found && ::PyMapping_Check(obj.ptr()))
  {
    ::PyMapping_SetItemString(obj.ptr(), *name, newval.ptr());    
  }
  else 
  {
  #ifdef SUPPORT_PROPERTY
    if (found)
    {
      py::object attr = obj.attr(*name);

      if (PyObject_TypeCheck(attr.ptr(), &::PyProperty_Type))
      {
        py::object setter = attr.attr("fset");

        if (setter.ptr() == Py_None)
          throw CJavascriptException("can't set attribute", ::PyExc_AttributeError);

        setter(newval);    

        return value;
      }
    }
  #endif
    obj.attr(*name) = newval;  
  }

  return value;
 
  END_HANDLE_EXCEPTION(v8::Undefined());
}

v8::Handle<v8::Integer> CPythonObject::NamedQuery(
  v8::Local<v8::String> prop, const v8::AccessorInfo& info)
{
  TRY_HANDLE_EXCEPTION()

  v8::HandleScope handle_scope;
  CPythonGIL python_gil;

  TERMINATE_EXECUTION_CHECK(v8::Handle<v8::Integer>())

  py::object obj = CJavascriptObject::Wrap(info.Holder());  

  v8::String::Utf8Value name(prop);

  bool exists = PyGen_Check(obj.ptr()) || ::PyObject_HasAttrString(obj.ptr(), *name) || 
                (::PyMapping_Check(obj.ptr()) && ::PyMapping_HasKeyString(obj.ptr(), *name));

  return exists ? handle_scope.Close(v8::Integer::New(v8::None)) : v8::Handle<v8::Integer>();

  END_HANDLE_EXCEPTION(v8::Handle<v8::Integer>())
}

v8::Handle<v8::Boolean> CPythonObject::NamedDeleter(
  v8::Local<v8::String> prop, const v8::AccessorInfo& info)
{
  TRY_HANDLE_EXCEPTION()

  v8::HandleScope handle_scope;
  CPythonGIL python_gil;

  TERMINATE_EXECUTION_CHECK(v8::Handle<v8::Boolean>())

  py::object obj = CJavascriptObject::Wrap(info.Holder());  

  v8::String::Utf8Value name(prop);
 
  if (!::PyObject_HasAttrString(obj.ptr(), *name) &&
      ::PyMapping_Check(obj.ptr()) && 
      ::PyMapping_HasKeyString(obj.ptr(), *name))
  {
    return v8::Boolean::New(-1 != ::PyMapping_DelItemString(obj.ptr(), *name));
  }
  else
  {
  #ifdef SUPPORT_PROPERTY
    py::object attr = obj.attr(*name);    

    if (::PyObject_HasAttrString(obj.ptr(), *name) &&
        PyObject_TypeCheck(attr.ptr(), &::PyProperty_Type))
    {
      py::object deleter = attr.attr("fdel");

      if (deleter.ptr() == Py_None)
        throw CJavascriptException("can't delete attribute", ::PyExc_AttributeError); 

      return v8::Boolean::New(py::extract<bool>(deleter()));
    }
    else
    {
      return v8::Boolean::New(-1 != ::PyObject_DelAttrString(obj.ptr(), *name));
    }
  #else
    return v8::Boolean::New(-1 != ::PyObject_DelAttrString(obj.ptr(), *name));
  #endif
  }

  END_HANDLE_EXCEPTION(v8::False())
}

v8::Handle<v8::Array> CPythonObject::NamedEnumerator(const v8::AccessorInfo& info)
{
  TRY_HANDLE_EXCEPTION()

  v8::HandleScope handle_scope;  
  CPythonGIL python_gil;

  TERMINATE_EXECUTION_CHECK(v8::Handle<v8::Array>())

  py::object obj = CJavascriptObject::Wrap(info.Holder());

  py::list keys;
  bool filter_name = false;
  
  if (::PySequence_Check(obj.ptr()))
  {
    return v8::Handle<v8::Array>();
  }
  else if (::PyMapping_Check(obj.ptr()))
  {
    keys = py::list(py::handle<>(PyMapping_Keys(obj.ptr())));    
  }
  else if (PyGen_CheckExact(obj.ptr()))
  {
    py::object iter(py::handle<>(::PyObject_GetIter(obj.ptr())));

    PyObject *item = NULL;

    while (NULL != (item = ::PyIter_Next(iter.ptr())))
    {
      keys.append(py::object(py::handle<>(item)));
    } 
  }
  else
  {
    keys = py::list(py::handle<>(::PyObject_Dir(obj.ptr())));
    filter_name = true;
  }

  Py_ssize_t len = PyList_GET_SIZE(keys.ptr());
  v8::Handle<v8::Array> result = v8::Array::New(len);

  if (len > 0)
  {
    for (Py_ssize_t i=0; i<len; i++)
    {
      PyObject *item = PyList_GET_ITEM(keys.ptr(), i);

      if (filter_name && PyBytes_CheckExact(item))
      {
        py::str name(py::handle<>(py::borrowed(item)));

        // FIXME Are there any methods to avoid such a dirty work?

        if (name.startswith("__") && name.endswith("__"))
          continue;
      }

      result->Set(v8::Uint32::New(i), Wrap(py::object(py::handle<>(py::borrowed(item)))));
    }

    return handle_scope.Close(result);
  }

  END_HANDLE_EXCEPTION(v8::Handle<v8::Array>())
}

v8::Handle<v8::Value> CPythonObject::IndexedGetter(
  uint32_t index, const v8::AccessorInfo& info)
{
  TRY_HANDLE_EXCEPTION()

  v8::HandleScope handle_scope;
  CPythonGIL python_gil;

  TERMINATE_EXECUTION_CHECK(v8::Undefined())

  py::object obj = CJavascriptObject::Wrap(info.Holder());  

  if (PyGen_Check(obj.ptr())) return v8::Undefined();

  if (::PySequence_Check(obj.ptr()))
  {
    if ((Py_ssize_t) index < ::PySequence_Size(obj.ptr()))
    {
      py::object ret(py::handle<>(::PySequence_GetItem(obj.ptr(), index)));

      return handle_scope.Close(Wrap(ret));
    }
    else
    {
      return v8::Undefined();
    }
  }
  else if (::PyMapping_Check(obj.ptr()))
  { 
    char buf[65];

    snprintf(buf, sizeof(buf), "%d", index);

    PyObject *value = ::PyMapping_GetItemString(obj.ptr(), buf);

    if (!value) 
    {
      py::long_ key(index);

      value = ::PyObject_GetItem(obj.ptr(), key.ptr());
    }

    if (value)
    {
      return handle_scope.Close(Wrap(py::object(py::handle<>(value))));  
    }
  }
  
  END_HANDLE_EXCEPTION(v8::Undefined())
}
v8::Handle<v8::Value> CPythonObject::IndexedSetter(
  uint32_t index, v8::Local<v8::Value> value, const v8::AccessorInfo& info)
{
  TRY_HANDLE_EXCEPTION()

  v8::HandleScope handle_scope;
  CPythonGIL python_gil;

  TERMINATE_EXECUTION_CHECK(v8::Undefined())

  py::object obj = CJavascriptObject::Wrap(info.Holder());  

  if (::PySequence_Check(obj.ptr()))
  {
    if (::PySequence_SetItem(obj.ptr(), index, CJavascriptObject::Wrap(value).ptr()) < 0)
      v8::ThrowException(v8::Exception::Error(v8::String::NewSymbol("fail to set indexed value")));
  }
  else if (::PyMapping_Check(obj.ptr()))
  { 
    char buf[65];

    snprintf(buf, sizeof(buf), "%d", index);

    if (::PyMapping_SetItemString(obj.ptr(), buf, CJavascriptObject::Wrap(value).ptr()) < 0)
      v8::ThrowException(v8::Exception::Error(v8::String::NewSymbol("fail to set named value")));
  }

  return value;
  
  END_HANDLE_EXCEPTION(v8::Undefined())
}
v8::Handle<v8::Integer> CPythonObject::IndexedQuery(
  uint32_t index, const v8::AccessorInfo& info)
{
  TRY_HANDLE_EXCEPTION()

  v8::HandleScope handle_scope;
  CPythonGIL python_gil;

  TERMINATE_EXECUTION_CHECK(v8::Handle<v8::Integer>())

  py::object obj = CJavascriptObject::Wrap(info.Holder());  

  if (PyGen_Check(obj.ptr())) return handle_scope.Close(v8::Integer::New(v8::ReadOnly));

  if (::PySequence_Check(obj.ptr()))
  {
    if ((Py_ssize_t) index < ::PySequence_Size(obj.ptr()))
    {
      return handle_scope.Close(v8::Integer::New(v8::None));
    }    
    else
    {
      return v8::Handle<v8::Integer>();
    }
  }
  else if (::PyMapping_Check(obj.ptr()))
  { 
    char buf[65];

    snprintf(buf, sizeof(buf), "%d", index);

    if (::PyMapping_HasKeyString(obj.ptr(), buf) ||
        ::PyMapping_HasKey(obj.ptr(), py::long_(index).ptr()))
    {
      return handle_scope.Close(v8::Integer::New(v8::None));
    }
    else
    {
      return v8::Handle<v8::Integer>();
    }
  }  

  END_HANDLE_EXCEPTION(v8::Handle<v8::Integer>())
}
v8::Handle<v8::Boolean> CPythonObject::IndexedDeleter(
  uint32_t index, const v8::AccessorInfo& info)
{
  TRY_HANDLE_EXCEPTION()

  v8::HandleScope handle_scope;
  CPythonGIL python_gil;

  TERMINATE_EXECUTION_CHECK(v8::Handle<v8::Boolean>())

  py::object obj = CJavascriptObject::Wrap(info.Holder());  

  if (::PySequence_Check(obj.ptr()) && (Py_ssize_t) index < ::PySequence_Size(obj.ptr()))
  {
    return v8::Boolean::New(0 <= ::PySequence_DelItem(obj.ptr(), index));
  }
  else if (::PyMapping_Check(obj.ptr()))
  { 
    char buf[65];

    snprintf(buf, sizeof(buf), "%d", index);

    return v8::Boolean::New(PyMapping_DelItemString(obj.ptr(), buf) == 0);
  }

  END_HANDLE_EXCEPTION(v8::False())
}

v8::Handle<v8::Array> CPythonObject::IndexedEnumerator(const v8::AccessorInfo& info)
{
  TRY_HANDLE_EXCEPTION()

  v8::HandleScope handle_scope;  
  CPythonGIL python_gil;

  TERMINATE_EXECUTION_CHECK(v8::Handle<v8::Array>())

  py::object obj = CJavascriptObject::Wrap(info.Holder());

  Py_ssize_t len = ::PySequence_Check(obj.ptr()) ? ::PySequence_Size(obj.ptr()) : 0;

  v8::Handle<v8::Array> result = v8::Array::New(len);

  for (Py_ssize_t i=0; i<len; i++)
  {
    result->Set(v8::Uint32::New(i), v8::Int32::New(i)->ToString());
  }

  return handle_scope.Close(result);

  END_HANDLE_EXCEPTION(v8::Handle<v8::Array>())
}

#define GEN_ARG(z, n, data) CJavascriptObject::Wrap(args[n])
#define GEN_ARGS(count) BOOST_PP_ENUM(count, GEN_ARG, NULL)

#define GEN_CASE_PRED(r, state) \
  BOOST_PP_NOT_EQUAL( \
    BOOST_PP_TUPLE_ELEM(2, 0, state), \
    BOOST_PP_INC(BOOST_PP_TUPLE_ELEM(2, 1, state)) \
  ) \
  /**/

#define GEN_CASE_OP(r, state) \
  ( \
    BOOST_PP_INC(BOOST_PP_TUPLE_ELEM(2, 0, state)), \
    BOOST_PP_TUPLE_ELEM(2, 1, state) \
  ) \
  /**/

#define GEN_CASE_MACRO(r, state) \
  case BOOST_PP_TUPLE_ELEM(2, 0, state): { \
    result = self(GEN_ARGS(BOOST_PP_TUPLE_ELEM(2, 0, state))); \
    break; \
  } \
  /**/

v8::Handle<v8::Value> CPythonObject::Caller(const v8::Arguments& args)
{
  TRY_HANDLE_EXCEPTION()

  v8::HandleScope handle_scope;
  CPythonGIL python_gil;

  TERMINATE_EXECUTION_CHECK(v8::Undefined())

  py::object self;
  
  if (!args.Data().IsEmpty() && args.Data()->IsExternal())
  {
    v8::Handle<v8::External> field = v8::Handle<v8::External>::Cast(args.Data());

    self = *static_cast<py::object *>(field->Value());
  }
  else
  {
    self = CJavascriptObject::Wrap(args.This());
  }

  py::object result;

  switch (args.Length())
  {
    BOOST_PP_FOR((0, 10), GEN_CASE_PRED, GEN_CASE_OP, GEN_CASE_MACRO)
  default:
    return v8::ThrowException(v8::Exception::Error(v8::String::NewSymbol("too many arguments")));
  }

  return handle_scope.Close(Wrap(result));
  
  END_HANDLE_EXCEPTION(v8::Undefined())
}

void CPythonObject::SetupObjectTemplate(v8::Handle<v8::ObjectTemplate> clazz)
{
  clazz->SetInternalFieldCount(1);
  clazz->SetNamedPropertyHandler(NamedGetter, NamedSetter, NamedQuery, NamedDeleter, NamedEnumerator);
  clazz->SetIndexedPropertyHandler(IndexedGetter, IndexedSetter, IndexedQuery, IndexedDeleter, IndexedEnumerator);
  clazz->SetCallAsFunctionHandler(Caller);
}

v8::Persistent<v8::ObjectTemplate> CPythonObject::CreateObjectTemplate(void)
{
  v8::HandleScope handle_scope;

  v8::Handle<v8::ObjectTemplate> clazz = v8::ObjectTemplate::New();

  SetupObjectTemplate(clazz);

  return v8::Persistent<v8::ObjectTemplate>::New(clazz);
}

bool CPythonObject::IsWrapped(v8::Handle<v8::Object> obj)
{
  return obj->InternalFieldCount() == 1;
}

py::object CPythonObject::Unwrap(v8::Handle<v8::Object> obj)
{
  v8::HandleScope handle_scope;

  v8::Handle<v8::External> payload = v8::Handle<v8::External>::Cast(obj->GetInternalField(0));

  return *static_cast<py::object *>(payload->Value());
}

void CPythonObject::Dispose(v8::Handle<v8::Value> value)
{
  v8::HandleScope handle_scope;

  if (value->IsObject())
  {
    v8::Handle<v8::Object> obj = value->ToObject();

    if (IsWrapped(obj))
    {
      Py_DECREF(CPythonObject::Unwrap(obj).ptr());
    }
  }
}

v8::Handle<v8::Value> CPythonObject::Wrap(py::object obj)
{
  v8::HandleScope handle_scope;

  v8::Handle<v8::Value> value;
  
#ifdef SUPPORT_TRACE_LIFECYCLE
  value = ObjectTracer::FindCache(obj);

  if (value.IsEmpty()) 
#endif

  value = WrapInternal(obj);  

  return handle_scope.Close(value);
}

v8::Handle<v8::Value> CPythonObject::WrapInternal(py::object obj)
{
  assert(v8::Context::InContext());

  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;

  CPythonGIL python_gil;

  TERMINATE_EXECUTION_CHECK(v8::Undefined())

  if (obj.ptr() == Py_None) return v8::Null();
  if (obj.ptr() == Py_True) return v8::True();
  if (obj.ptr() == Py_False) return v8::False();

  py::extract<CJavascriptObject&> extractor(obj);

  if (extractor.check())
  {
    CJavascriptObject& jsobj = extractor();

    if (jsobj.Object().IsEmpty())
    {
      ILazyObject *pLazyObject = dynamic_cast<ILazyObject *>(&jsobj);

      if (pLazyObject) pLazyObject->LazyConstructor();
    }

    if (jsobj.Object().IsEmpty())
    {
      throw CJavascriptException("Refer to a null object", ::PyExc_AttributeError);    
    }
    
  #ifdef SUPPORT_TRACE_LIFECYCLE
    py::object *object = new py::object(obj);

    ObjectTracer::Trace(jsobj.Object(), object);
  #endif

    return handle_scope.Close(jsobj.Object());
  }

  v8::Handle<v8::Value> result;

#if PY_MAJOR_VERSION < 3
  if (PyInt_CheckExact(obj.ptr()))
  {
    result = v8::Integer::New(::PyInt_AsLong(obj.ptr()));    
  }
  else
#endif
  if (PyLong_CheckExact(obj.ptr()))
  {
    result = v8::Integer::New(::PyLong_AsLong(obj.ptr()));
  }
  else if (PyBool_Check(obj.ptr()))
  {
    result = v8::Boolean::New(py::extract<bool>(obj));
  }
  else if (PyBytes_CheckExact(obj.ptr()) ||
           PyUnicode_CheckExact(obj.ptr()))
  {
    result = ToString(obj);
  }
  else if (PyFloat_CheckExact(obj.ptr()))
  {   
    result = v8::Number::New(py::extract<double>(obj));
  }
  else if (PyDateTime_CheckExact(obj.ptr()) || PyDate_CheckExact(obj.ptr()))
  {
    tm ts = { 0 };

    ts.tm_year = PyDateTime_GET_YEAR(obj.ptr()) - 1900;
    ts.tm_mon = PyDateTime_GET_MONTH(obj.ptr()) - 1;
    ts.tm_mday = PyDateTime_GET_DAY(obj.ptr());
    ts.tm_hour = PyDateTime_DATE_GET_HOUR(obj.ptr());
    ts.tm_min = PyDateTime_DATE_GET_MINUTE(obj.ptr());
    ts.tm_sec = PyDateTime_DATE_GET_SECOND(obj.ptr());
    ts.tm_isdst = -1;
    
    int ms = PyDateTime_DATE_GET_MICROSECOND(obj.ptr());

    result = v8::Date::New(((double) mktime(&ts)) * 1000 + ms / 1000);
  }
  else if (PyTime_CheckExact(obj.ptr()))
  {
    tm ts = { 0 };

    ts.tm_hour = PyDateTime_TIME_GET_HOUR(obj.ptr()) - 1;
    ts.tm_min = PyDateTime_TIME_GET_MINUTE(obj.ptr());
    ts.tm_sec = PyDateTime_TIME_GET_SECOND(obj.ptr());

    int ms = PyDateTime_TIME_GET_MICROSECOND(obj.ptr());

    result = v8::Date::New(((double) mktime(&ts)) * 1000 + ms / 1000);    
  }
  else if (PyCFunction_Check(obj.ptr()) || PyFunction_Check(obj.ptr()) || 
           PyMethod_Check(obj.ptr()) || PyType_Check(obj.ptr()))
  {
    v8::Handle<v8::FunctionTemplate> func_tmpl = v8::FunctionTemplate::New();    
    py::object *object = new py::object(obj);

    func_tmpl->SetCallHandler(Caller, v8::External::New(object));

    if (PyType_Check(obj.ptr()))
    {
      v8::Handle<v8::String> cls_name = v8::String::New(py::extract<const char *>(obj.attr("__name__"))());

      func_tmpl->SetClassName(cls_name);
    }

    result = func_tmpl->GetFunction();

  #ifdef SUPPORT_TRACE_LIFECYCLE
    if (!result.IsEmpty()) ObjectTracer::Trace(result, object);
  #endif
  }
  else
  {
    static v8::Persistent<v8::ObjectTemplate> s_template = CreateObjectTemplate();

    v8::Handle<v8::Object> instance = s_template->NewInstance();

    if (!instance.IsEmpty())
    {
      py::object *object = new py::object(obj);

      instance->SetInternalField(0, v8::External::New(object));

    #ifdef SUPPORT_TRACE_LIFECYCLE
      ObjectTracer::Trace(instance, object);
    #endif
    }

    result = instance;
  }

  if (result.IsEmpty()) CJavascriptException::ThrowIf(try_catch);

  return handle_scope.Close(result);
}

void CJavascriptObject::CheckAttr(v8::Handle<v8::String> name) const
{
  assert(v8::Context::InContext());

  if (!m_obj->Has(name))
  {
    std::ostringstream msg;
      
    msg << "'" << *v8::String::Utf8Value(m_obj->ObjectProtoToString()) 
        << "' object has no attribute '" << *v8::String::Utf8Value(name) << "'";

    throw CJavascriptException(msg.str(), ::PyExc_AttributeError);
  }
}

py::object CJavascriptObject::GetAttr(const std::string& name)
{
  CHECK_V8_CONTEXT();

  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;

  v8::Handle<v8::String> attr_name = DecodeUtf8(name);

  CheckAttr(attr_name);

  v8::Handle<v8::Value> attr_value = m_obj->Get(attr_name);

  if (attr_value.IsEmpty()) 
    CJavascriptException::ThrowIf(try_catch);

  return CJavascriptObject::Wrap(attr_value, m_obj);
}

void CJavascriptObject::SetAttr(const std::string& name, py::object value)
{
  CHECK_V8_CONTEXT();

  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;

  v8::Handle<v8::String> attr_name = DecodeUtf8(name);
  v8::Handle<v8::Value> attr_obj = CPythonObject::Wrap(value);

  if (m_obj->Has(attr_name))
  {
    v8::Handle<v8::Value> UNUSED_VAR(attr_value) = m_obj->Get(attr_name);
  }

  if (!m_obj->Set(attr_name, attr_obj)) 
    CJavascriptException::ThrowIf(try_catch);
}
void CJavascriptObject::DelAttr(const std::string& name)
{
  CHECK_V8_CONTEXT();

  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;

  v8::Handle<v8::String> attr_name = DecodeUtf8(name);

  CheckAttr(attr_name);

  if (!m_obj->Delete(attr_name)) 
    CJavascriptException::ThrowIf(try_catch);
}
py::list CJavascriptObject::GetAttrList(void)
{
  CHECK_V8_CONTEXT();

  v8::HandleScope handle_scope;
  CPythonGIL python_gil;

  py::list attrs;

  TERMINATE_EXECUTION_CHECK(attrs);

  v8::TryCatch try_catch;

  v8::Handle<v8::Array> props = m_obj->GetPropertyNames();

  for (size_t i=0; i<props->Length(); i++)
  {    
    attrs.append(CJavascriptObject::Wrap(props->Get(v8::Integer::New(i))));
  }

  if (try_catch.HasCaught()) CJavascriptException::ThrowIf(try_catch);

  return attrs;
}

int CJavascriptObject::GetIdentityHash(void) 
{ 
  CHECK_V8_CONTEXT(); 
  
  return m_obj->GetIdentityHash(); 
}

CJavascriptObjectPtr CJavascriptObject::Clone(void)
{
  CHECK_V8_CONTEXT();

  v8::HandleScope handle_scope;

  return CJavascriptObjectPtr(new CJavascriptObject(m_obj->Clone()));
}

bool CJavascriptObject::Contains(const std::string& name)
{
  CHECK_V8_CONTEXT();

  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;

  bool found = m_obj->Has(DecodeUtf8(name));
  
  if (try_catch.HasCaught()) CJavascriptException::ThrowIf(try_catch);

  return found;
}

bool CJavascriptObject::Equals(CJavascriptObjectPtr other) const
{
  CHECK_V8_CONTEXT();

  v8::HandleScope handle_scope;
  
  return other.get() && m_obj->Equals(other->m_obj);
}

void CJavascriptObject::Dump(std::ostream& os) const
{
  CHECK_V8_CONTEXT();

  v8::HandleScope handle_scope;

  if (m_obj.IsEmpty())
    os << "None";
  else if (m_obj->IsInt32())
    os << m_obj->Int32Value();
  else if (m_obj->IsNumber())
    os << m_obj->NumberValue();
  else if (m_obj->IsBoolean())
    os << m_obj->BooleanValue();
  else if (m_obj->IsNull())
    os << "None";
  else if (m_obj->IsUndefined())
    os << "N/A";
  else if (m_obj->IsString())  
    os << *v8::String::Utf8Value(v8::Handle<v8::String>::Cast(m_obj));  
  else 
  {
    v8::Handle<v8::String> s = m_obj->ToString();

    if (s.IsEmpty())
      s = m_obj->ObjectProtoToString();

    if (!s.IsEmpty())
      os << *v8::String::Utf8Value(s);
  }
}

CJavascriptObject::operator long() const 
{ 
  CHECK_V8_CONTEXT();

  v8::HandleScope handle_scope;

  if (m_obj.IsEmpty())
    throw CJavascriptException("argument must be a string or a number, not 'NoneType'", ::PyExc_TypeError);

  return m_obj->Int32Value(); 
}
CJavascriptObject::operator double() const 
{ 
  CHECK_V8_CONTEXT();

  v8::HandleScope handle_scope;

  if (m_obj.IsEmpty())
    throw CJavascriptException("argument must be a string or a number, not 'NoneType'", ::PyExc_TypeError);

  return m_obj->NumberValue(); 
}

CJavascriptObject::operator bool() const
{
  CHECK_V8_CONTEXT();

  v8::HandleScope handle_scope;

  if (m_obj.IsEmpty()) return false;

  return m_obj->BooleanValue();
}

py::object CJavascriptObject::Wrap(v8::Handle<v8::Value> value, v8::Handle<v8::Object> self)
{
  assert(v8::Context::InContext());

  v8::HandleScope handle_scope;

  if (value.IsEmpty() || value->IsNull() || value->IsUndefined()) return py::object();
  if (value->IsTrue()) return py::object(py::handle<>(py::borrowed(Py_True)));
  if (value->IsFalse()) return py::object(py::handle<>(py::borrowed(Py_False)));

  if (value->IsInt32()) return py::object(value->Int32Value());  
  if (value->IsString())
  {
    v8::String::Utf8Value str(v8::Handle<v8::String>::Cast(value));

    return py::str(*str, str.length());
  }
  if (value->IsStringObject())
  {
    v8::String::Utf8Value str(value.As<v8::StringObject>()->StringValue());

    return py::str(*str, str.length());
  }
  if (value->IsBoolean()) 
  {
    return py::object(py::handle<>(py::borrowed(value->BooleanValue() ? Py_True : Py_False)));
  }
  if (value->IsBooleanObject())
  {
    return py::object(py::handle<>(py::borrowed(value.As<v8::BooleanObject>()->BooleanValue() ? Py_True : Py_False)));
  }
  if (value->IsNumber()) 
  {
    return py::object(py::handle<>(::PyFloat_FromDouble(value->NumberValue())));
  }
  if (value->IsNumberObject())
  {
    return py::object(py::handle<>(::PyFloat_FromDouble(value.As<v8::NumberObject>()->NumberValue())));
  }
  if (value->IsDate())
  {
    double n = v8::Handle<v8::Date>::Cast(value)->NumberValue();

    time_t ts = (time_t) floor(n / 1000);

    tm *t = localtime(&ts);

    return py::object(py::handle<>(::PyDateTime_FromDateAndTime(
      t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, 
      ((long long) floor(n)) % 1000 * 1000)));
  }

  return Wrap(value->ToObject(), self);
}

py::object CJavascriptObject::Wrap(v8::Handle<v8::Object> obj, v8::Handle<v8::Object> self) 
{
  v8::HandleScope handle_scope;

  if (obj.IsEmpty())
  {
    return py::object();
  }
  else if (obj->IsArray())
  {
    v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(obj);

    return Wrap(new CJavascriptArray(array));
  }
  else if (obj->IsFunction())
  {
    if (CPythonObject::IsWrapped(obj)) return CPythonObject::Unwrap(obj);    

    return Wrap(new CJavascriptFunction(self, v8::Handle<v8::Function>::Cast(obj)));
  }
  else if (CPythonObject::IsWrapped(obj))
  {
    return CPythonObject::Unwrap(obj);
  }
  
  return Wrap(new CJavascriptObject(obj));
}

py::object CJavascriptObject::Wrap(CJavascriptObject *obj)
{
  CPythonGIL python_gil;

  TERMINATE_EXECUTION_CHECK(py::object())

  return py::object(py::handle<>(boost::python::converter::shared_ptr_to_python<CJavascriptObject>(CJavascriptObjectPtr(obj))));
}

void CJavascriptArray::LazyConstructor(void)
{
  if (!m_obj.IsEmpty()) return;

  v8::HandleScope handle_scope;

  v8::Handle<v8::Array> array;

  if (m_items.ptr() == Py_None)
  {
    array = v8::Array::New(m_size);
  }
#if PY_MAJOR_VERSION < 3
  else if (PyInt_CheckExact(m_items.ptr()))
  {
    m_size = PyInt_AS_LONG(m_items.ptr());
    array = v8::Array::New(m_size);
  }
#endif
  else if (PyLong_CheckExact(m_items.ptr()))
  {
    m_size = PyLong_AsLong(m_items.ptr());
    array = v8::Array::New(m_size);
  }
  else if (PyList_Check(m_items.ptr())) 
  {
    m_size = PyList_GET_SIZE(m_items.ptr());
    array = v8::Array::New(m_size);

    for (Py_ssize_t i=0; i<m_size; i++)
    {
      array->Set(v8::Uint32::New(i), CPythonObject::Wrap(py::object(py::handle<>(py::borrowed(PyList_GET_ITEM(m_items.ptr(), i))))));
    }    
  }
  else if (PyTuple_Check(m_items.ptr()))
  {
    m_size = PyTuple_GET_SIZE(m_items.ptr());
    array = v8::Array::New(m_size);

    for (Py_ssize_t i=0; i<m_size; i++)
    {
      array->Set(v8::Uint32::New(i), CPythonObject::Wrap(py::object(py::handle<>(py::borrowed(PyTuple_GET_ITEM(m_items.ptr(), i))))));
    }
  }
  else if (PyGen_Check(m_items.ptr()))
  {
    array = v8::Array::New();

    py::object iter(py::handle<>(::PyObject_GetIter(m_items.ptr())));

    m_size = 0;
    PyObject *item = NULL;

    while (NULL != (item = ::PyIter_Next(iter.ptr())))
    {
      array->Set(v8::Uint32::New(m_size++), CPythonObject::Wrap(py::object(py::handle<>(py::borrowed(item)))));
    } 
  }

  m_obj = v8::Persistent<v8::Object>::New(array); 
}
size_t CJavascriptArray::Length(void) 
{
  CHECK_V8_CONTEXT();
    
  LazyConstructor();

  v8::HandleScope handle_scope;

  return v8::Handle<v8::Array>::Cast(m_obj)->Length();
}
py::object CJavascriptArray::GetItem(py::object key)
{
  CHECK_V8_CONTEXT();
    
  LazyConstructor();

  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;
  
  if (PySlice_Check(key.ptr()))
  {
    Py_ssize_t arrayLen = v8::Handle<v8::Array>::Cast(m_obj)->Length();
    Py_ssize_t start, stop, step, sliceLen;
    
    if (0 == ::PySlice_GetIndicesEx(PySlice_Cast(key.ptr()), arrayLen, &start, &stop, &step, &sliceLen))
    {
      py::list slice;
      
      for (Py_ssize_t idx=start; idx<stop; idx+=step)
      {
        v8::Handle<v8::Value> value = m_obj->Get(v8::Integer::New((uint32_t) idx));
        
        if (value.IsEmpty()) CJavascriptException::ThrowIf(try_catch);
        
        slice.append(CJavascriptObject::Wrap(value, m_obj));
      }
      
      return slice;
    }
  }
  else if (PyInt_Check(key.ptr()) || PyLong_Check(key.ptr()))
  {
    uint32_t idx = PyInt_Check(key.ptr()) ? (uint32_t) ::PyInt_AsUnsignedLongMask(key.ptr()) : (uint32_t) ::PyLong_AsUnsignedLong(key.ptr());
    
    if (!m_obj->Has(idx)) return py::object();
    
    v8::Handle<v8::Value> value = m_obj->Get(v8::Integer::New(idx));
    
    if (value.IsEmpty()) CJavascriptException::ThrowIf(try_catch);
    
    return CJavascriptObject::Wrap(value, m_obj);
  }
  
  throw CJavascriptException("list indices must be integers", ::PyExc_TypeError);
}

py::object CJavascriptArray::SetItem(py::object key, py::object value)
{
  CHECK_V8_CONTEXT();
    
  LazyConstructor();

  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;
  
  if (PySlice_Check(key.ptr()))
  {
    PyObject *values = ::PySequence_Fast(value.ptr(), "can only assign an iterable");
    
    if (values)
    {
      Py_ssize_t itemSize = PySequence_Fast_GET_SIZE(value.ptr());
      PyObject **items = PySequence_Fast_ITEMS(value.ptr());
      
      Py_ssize_t arrayLen = v8::Handle<v8::Array>::Cast(m_obj)->Length();
      Py_ssize_t start, stop, step, sliceLen;
      
      if (0 == ::PySlice_GetIndicesEx(PySlice_Cast(key.ptr()), arrayLen, &start, &stop, &step, &sliceLen))
      {
        if (itemSize != sliceLen)
        {
          v8i::Handle<v8i::JSArray> array = v8::Utils::OpenHandle(*m_obj);
          
          array->set_length(v8i::Smi::FromInt(arrayLen - sliceLen + itemSize));
          
          if (itemSize < sliceLen)
          {          
            Py_ssize_t diff = sliceLen - itemSize;
            
            for (Py_ssize_t idx=start+itemSize; idx<arrayLen-diff; idx++)
            {
              m_obj->Set(idx, m_obj->Get((uint32_t) (idx + diff)));
            }
            for (Py_ssize_t idx=arrayLen-1; idx >arrayLen-diff-1; idx--)
            {
              m_obj->Delete((uint32_t) idx);
            }
          }
          else if (itemSize > sliceLen)
          {
            Py_ssize_t diff = itemSize - sliceLen;
            
            for (Py_ssize_t idx=arrayLen+diff-1; idx>stop-1; idx--)
            {
              m_obj->Set(idx, m_obj->Get((uint32_t) (idx - diff)));
            }
          }
        }
        
        for (Py_ssize_t idx=0; idx<itemSize; idx++)
        {
          m_obj->Set((uint32_t) (start + idx), CPythonObject::Wrap(py::object(py::handle<>(py::borrowed(items[idx])))));
        }
      }
    }
  }
  else if (PyInt_Check(key.ptr()) || PyLong_Check(key.ptr()))
  {
    uint32_t idx = PyInt_Check(key.ptr()) ? (uint32_t) ::PyInt_AsUnsignedLongMask(key.ptr()) : (uint32_t) ::PyLong_AsUnsignedLong(key.ptr());
        
    if (!m_obj->Set(v8::Integer::New(idx), CPythonObject::Wrap(value)))
      CJavascriptException::ThrowIf(try_catch);
  }

  return value;
}
py::object CJavascriptArray::DelItem(py::object key)
{
  CHECK_V8_CONTEXT();
    
  LazyConstructor();

  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;

  if (PySlice_Check(key.ptr()))
  {
    Py_ssize_t arrayLen = v8::Handle<v8::Array>::Cast(m_obj)->Length();
    Py_ssize_t start, stop, step, sliceLen;
    
    if (0 == ::PySlice_GetIndicesEx(PySlice_Cast(key.ptr()), arrayLen, &start, &stop, &step, &sliceLen))
    {
      for (Py_ssize_t idx=stop; idx<arrayLen; idx++)
      {
        m_obj->Set((uint32_t) (idx - sliceLen), m_obj->Get((uint32_t) idx));
      }
      
      for (Py_ssize_t idx=arrayLen-1; idx>arrayLen-sliceLen-1; idx--)
      {
        m_obj->Delete((uint32_t)idx);
      }
      
      v8i::Handle<v8i::JSArray> array = v8::Utils::OpenHandle(*m_obj);
      
      array->set_length(v8i::Smi::FromInt(arrayLen - sliceLen));
    }
    
    return py::object();
  }
  else if (PyInt_Check(key.ptr()) || PyLong_Check(key.ptr()))
  {
    uint32_t idx = PyInt_Check(key.ptr()) ? (uint32_t) ::PyInt_AsUnsignedLongMask(key.ptr()) : (uint32_t) ::PyLong_AsUnsignedLong(key.ptr());
    
    py::object value;
    
    if (m_obj->Has(idx))
      value = CJavascriptObject::Wrap(m_obj->Get(v8::Integer::New(idx)), m_obj);
    
    if (!m_obj->Delete(idx))
      CJavascriptException::ThrowIf(try_catch);
    
    return value;
  }
  
  throw CJavascriptException("list indices must be integers", ::PyExc_TypeError);
}

bool CJavascriptArray::Contains(py::object item)
{
  CHECK_V8_CONTEXT();
    
  LazyConstructor();

  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;

  for (size_t i=0; i<Length(); i++)
  {   
    if (m_obj->Has(i))
    {
      v8::Handle<v8::Value> value = m_obj->Get(v8::Integer::New(i));
      
      if (try_catch.HasCaught()) CJavascriptException::ThrowIf(try_catch);
      
      if (item == CJavascriptObject::Wrap(value, m_obj))
      {
        return true;
      }
    }
  }

  if (try_catch.HasCaught()) CJavascriptException::ThrowIf(try_catch);

  return false;
}

py::object CJavascriptFunction::CallWithArgs(py::tuple args, py::dict kwds)
{
  CHECK_V8_CONTEXT();

  size_t argc = ::PyTuple_Size(args.ptr());

  if (argc == 0) throw CJavascriptException("missed self argument", ::PyExc_TypeError);

  py::object self = args[0];
  py::extract<CJavascriptFunction&> extractor(self);

  if (!extractor.check()) throw CJavascriptException("missed self argument", ::PyExc_TypeError);

  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;  

  CJavascriptFunction& func = extractor();
  py::list argv(args.slice(1, py::_));

  return func.Call(func.m_self, argv, kwds);
}

py::object CJavascriptFunction::Call(v8::Handle<v8::Object> self, py::list args, py::dict kwds)
{
  CHECK_V8_CONTEXT(); 

  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;

  v8::Handle<v8::Function> func = v8::Handle<v8::Function>::Cast(m_obj);
  
  size_t args_count = ::PyList_Size(args.ptr()), kwds_count = ::PyMapping_Size(kwds.ptr());

  std::vector< v8::Handle<v8::Value> > params(args_count + kwds_count);

  for (size_t i=0; i<args_count; i++)
  {
    params[i] = CPythonObject::Wrap(args[i]);
  }

  py::list values = kwds.values();

  for (size_t i=0; i<kwds_count; i++)
  {
    params[args_count+i] = CPythonObject::Wrap(values[i]);
  }
  
  v8::Handle<v8::Value> result;

  Py_BEGIN_ALLOW_THREADS

  result = func->Call(
    self.IsEmpty() ? v8::Context::GetCurrent()->Global() : self, 
    params.size(), params.empty() ? NULL : &params[0]);

  Py_END_ALLOW_THREADS

  if (result.IsEmpty()) CJavascriptException::ThrowIf(try_catch);

  return CJavascriptObject::Wrap(result);
}

py::object CJavascriptFunction::CreateWithArgs(CJavascriptFunctionPtr proto, py::tuple args, py::dict kwds)
{
  CHECK_V8_CONTEXT();

  v8::HandleScope handle_scope;

  if (proto->Object().IsEmpty())
    throw CJavascriptException("Object prototype may only be an Object", ::PyExc_TypeError);

  v8::TryCatch try_catch;

  v8::Handle<v8::Function> func = v8::Handle<v8::Function>::Cast(proto->Object());

  size_t args_count = ::PyTuple_Size(args.ptr());
  
  std::vector< v8::Handle<v8::Value> > params(args_count);

  for (size_t i=0; i<args_count; i++)
  {
    params[i] = CPythonObject::Wrap(args[i]);
  }

  v8::Handle<v8::Object> result;

  Py_BEGIN_ALLOW_THREADS

  result = func->NewInstance(params.size(), params.empty() ? NULL : &params[0]);

  Py_END_ALLOW_THREADS

  if (result.IsEmpty()) CJavascriptException::ThrowIf(try_catch);

  size_t kwds_count = ::PyMapping_Size(kwds.ptr());
  py::list items = kwds.items();

  for (size_t i=0; i<kwds_count; i++)
  {
    py::tuple item(items[i]);

    py::str key(item[0]);
    py::object value = item[1];

    result->Set(ToString(key), CPythonObject::Wrap(value));
  }

  return CJavascriptObject::Wrap(result);
}

py::object CJavascriptFunction::Apply(CJavascriptObjectPtr self, py::list args, py::dict kwds)
{
  CHECK_V8_CONTEXT(); 

  v8::HandleScope handle_scope;

  return Call(self->Object(), args, kwds);
}

py::object CJavascriptFunction::Invoke(py::list args, py::dict kwds) 
{ 
  CHECK_V8_CONTEXT(); 

  v8::HandleScope handle_scope;

  return Call(m_self, args, kwds); 
}

const std::string CJavascriptFunction::GetName(void) const
{
  CHECK_V8_CONTEXT(); 

  v8::HandleScope handle_scope;

  v8::Handle<v8::Function> func = v8::Handle<v8::Function>::Cast(m_obj);  

  v8::String::Utf8Value name(v8::Handle<v8::String>::Cast(func->GetName()));

  return std::string(*name, name.length());
}

void CJavascriptFunction::SetName(const std::string& name)
{
  CHECK_V8_CONTEXT(); 

  v8::HandleScope handle_scope;
  
  v8::Handle<v8::Function> func = v8::Handle<v8::Function>::Cast(m_obj);  

  func->SetName(v8::String::New(name.c_str(), name.size()));
}

int CJavascriptFunction::GetLineNumber(void) const
{
  CHECK_V8_CONTEXT(); 

  v8::HandleScope handle_scope;

  v8::Handle<v8::Function> func = v8::Handle<v8::Function>::Cast(m_obj);  

  return func->GetScriptLineNumber();
}
int CJavascriptFunction::GetColumnNumber(void) const
{
  CHECK_V8_CONTEXT(); 

  v8::HandleScope handle_scope;

  v8::Handle<v8::Function> func = v8::Handle<v8::Function>::Cast(m_obj);  

  return func->GetScriptColumnNumber();
}
const std::string CJavascriptFunction::GetResourceName(void) const
{
  CHECK_V8_CONTEXT(); 

  v8::HandleScope handle_scope;

  v8::Handle<v8::Function> func = v8::Handle<v8::Function>::Cast(m_obj);  
  
  v8::String::Utf8Value name(v8::Handle<v8::String>::Cast(func->GetScriptOrigin().ResourceName()));

  return std::string(*name, name.length());
}
const std::string CJavascriptFunction::GetInferredName(void) const
{
  CHECK_V8_CONTEXT(); 

  v8::HandleScope handle_scope;

  v8::Handle<v8::Function> func = v8::Handle<v8::Function>::Cast(m_obj);  

  v8::String::Utf8Value name(v8::Handle<v8::String>::Cast(func->GetInferredName()));

  return std::string(*name, name.length());
}
int CJavascriptFunction::GetLineOffset(void) const
{
  CHECK_V8_CONTEXT(); 

  v8::HandleScope handle_scope;

  v8::Handle<v8::Function> func = v8::Handle<v8::Function>::Cast(m_obj);  

  return func->GetScriptOrigin().ResourceLineOffset()->Value();
}
int CJavascriptFunction::GetColumnOffset(void) const
{
  CHECK_V8_CONTEXT(); 

  v8::HandleScope handle_scope;

  v8::Handle<v8::Function> func = v8::Handle<v8::Function>::Cast(m_obj);  

  return func->GetScriptOrigin().ResourceColumnOffset()->Value();
}
py::object CJavascriptFunction::GetOwner(void) const 
{ 
  CHECK_V8_CONTEXT();  
  
  return CJavascriptObject::Wrap(m_self); 
}

#ifdef SUPPORT_TRACE_LIFECYCLE

ObjectTracer::ObjectTracer(v8::Handle<v8::Value> handle, py::object *object)
  : m_handle(v8::Persistent<v8::Value>::New(handle)), 
    m_object(object), m_living(GetLivingMapping())
{
}

ObjectTracer::~ObjectTracer()
{
  if (!m_handle.IsEmpty())
  {
    assert(m_handle.IsNearDeath());

    Dispose();
    
    m_living->erase(m_object->ptr());
  }
}

void ObjectTracer::Dispose(void)
{
  m_handle.ClearWeak();
  m_handle.Dispose();
  m_handle.Clear();
}

ObjectTracer& ObjectTracer::Trace(v8::Handle<v8::Value> handle, py::object *object)
{
  std::auto_ptr<ObjectTracer> tracer(new ObjectTracer(handle, object));

  tracer->Trace();

  return *tracer.release();
}

void ObjectTracer::Trace(void)
{
  m_handle.MakeWeak(v8::Isolate::GetCurrent(), this, WeakCallback);

  m_living->insert(std::make_pair(m_object->ptr(), this));
}

void ObjectTracer::WeakCallback(v8::Isolate* isolate, v8::Persistent<v8::Value> value, void* parameter)
{
  assert(value.IsNearDeath());

  std::auto_ptr<ObjectTracer> tracer(static_cast<ObjectTracer *>(parameter));

  assert(value == tracer->m_handle);
}

LivingMap * ObjectTracer::GetLivingMapping(void)
{
  if (!v8::Context::InContext()) return NULL;

  v8::HandleScope handle_scope;
  
  v8::Handle<v8::Context> ctxt = v8::Context::GetCurrent();

  v8::Handle<v8::Value> value = ctxt->Global()->GetHiddenValue(v8::String::NewSymbol("__living__"));

  if (!value.IsEmpty())
  {
    LivingMap *living = (LivingMap *) v8::External::Cast(*value)->Value();

    if (living) return living;
  }

  std::auto_ptr<LivingMap> living(new LivingMap());

  ctxt->Global()->SetHiddenValue(v8::String::NewSymbol("__living__"), v8::External::New(living.get()));
  
  ContextTracer::Trace(ctxt, living.get());

  return living.release();
}

v8::Persistent<v8::Value> ObjectTracer::FindCache(py::object obj)
{
  LivingMap *living = GetLivingMapping();

  if (living)
  {
    LivingMap::const_iterator it = living->find(obj.ptr());

    if (it != living->end())
    {
      return it->second->m_handle;
    }
  }

  return v8::Persistent<v8::Value>();
}

ContextTracer::ContextTracer(v8::Handle<v8::Context> ctxt, LivingMap *living)
  : m_ctxt(v8::Persistent<v8::Context>::New(v8::Isolate::GetCurrent(), ctxt)), m_living(living)
{
}

ContextTracer::~ContextTracer(void)
{
  m_ctxt->Global()->DeleteHiddenValue(v8::String::NewSymbol("__living__"));
  
  for (LivingMap::const_iterator it = m_living->begin(); it != m_living->end(); it++)
  {
    std::auto_ptr<ObjectTracer> tracer(it->second);
    
    tracer->Dispose();
  }
}

void ContextTracer::WeakCallback(v8::Isolate* isolate, v8::Persistent<v8::Value> value, void* parameter)
{
  delete (ContextTracer *) parameter;
}

void ContextTracer::Trace(v8::Handle<v8::Context> ctxt, LivingMap *living)
{
  ContextTracer *tracer = new ContextTracer(ctxt, living);
  
  tracer->Trace();
}

void ContextTracer::Trace(void)
{
  m_ctxt.MakeWeak(v8::Isolate::GetCurrent(), this, WeakCallback);
}

#endif // SUPPORT_TRACE_LIFECYCLE
