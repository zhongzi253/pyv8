#include "Exception.h"

#include <sstream>

std::ostream& operator<<(std::ostream& os, const CJavascriptException& ex)
{
  os << "JSError: " << ex.what();

  return os;
}

std::ostream& operator <<(std::ostream& os, const CJavascriptStackTrace& obj)
{ 
  obj.Dump(os);

  return os;
}

void CJavascriptException::Expose(void)
{
  py::class_<CJavascriptStackTrace>("JSStackTrace", py::no_init)
    .def("__len__", &CJavascriptStackTrace::GetFrameCount)
    .def("__getitem__", &CJavascriptStackTrace::GetFrame)

    .def("GetCurrentStackTrace", &CJavascriptStackTrace::GetCurrentStackTrace)
    .staticmethod("GetCurrentStackTrace")

    .def("__iter__", py::range(&CJavascriptStackTrace::begin, &CJavascriptStackTrace::end))

    .def(str(py::self))
    ;

  py::enum_<v8::StackTrace::StackTraceOptions>("JSStackTraceOptions")
    .value("LineNumber", v8::StackTrace::kLineNumber)
    .value("ColumnOffset", v8::StackTrace::kColumnOffset)
    .value("ScriptName", v8::StackTrace::kScriptName)
    .value("FunctionName", v8::StackTrace::kFunctionName)
    .value("IsEval", v8::StackTrace::kIsEval)
    .value("IsConstructor", v8::StackTrace::kIsConstructor)
    .value("Overview", v8::StackTrace::kOverview)
    .value("Detailed", v8::StackTrace::kDetailed)
    ;

  py::class_<CJavascriptStackFrame>("JSStackFrame", py::no_init)
    .add_property("lineNum", &CJavascriptStackFrame::GetLineNumber)
    .add_property("column", &CJavascriptStackFrame::GetColumn)
    .add_property("scriptName", &CJavascriptStackFrame::GetScriptName)
    .add_property("funcName", &CJavascriptStackFrame::GetFunctionName)
    .add_property("isEval", &CJavascriptStackFrame::IsEval)
    .add_property("isConstructor", &CJavascriptStackFrame::IsConstructor)
    ;

  py::objects::class_value_wrapper<boost::shared_ptr<CJavascriptStackTrace>, 
    py::objects::make_ptr_instance<CJavascriptStackTrace, 
    py::objects::pointer_holder<boost::shared_ptr<CJavascriptStackTrace>, CJavascriptStackTrace> > >();

  py::objects::class_value_wrapper<boost::shared_ptr<CJavascriptStackFrame>, 
    py::objects::make_ptr_instance<CJavascriptStackFrame, 
    py::objects::pointer_holder<boost::shared_ptr<CJavascriptStackFrame>, CJavascriptStackFrame> > >();

  py::class_<CJavascriptException>("_JSError", py::no_init)
    .def(str(py::self))

    .add_property("name", &CJavascriptException::GetName, "The exception name.")
    .add_property("message", &CJavascriptException::GetMessage, "The exception message.")
    .add_property("scriptName", &CJavascriptException::GetScriptName, "The script name which throw the exception.")
    .add_property("lineNum", &CJavascriptException::GetLineNumber, "The line number of error statement.")
    .add_property("startPos", &CJavascriptException::GetStartPosition, "The start position of error statement in the script.")
    .add_property("endPos", &CJavascriptException::GetEndPosition, "The end position of error statement in the script.")
    .add_property("startCol", &CJavascriptException::GetStartColumn, "The start column of error statement in the script.")
    .add_property("endCol", &CJavascriptException::GetEndColumn, "The end column of error statement in the script.")
    .add_property("sourceLine", &CJavascriptException::GetSourceLine, "The source line of error statement.")
    .add_property("stackTrace", &CJavascriptException::GetStackTrace, "The stack trace of error statement.")
    .def("print_tb", &CJavascriptException::PrintCallStack, (py::arg("file") = py::object()), "Print the stack trace of error statement.");

  py::register_exception_translator<CJavascriptException>(ExceptionTranslator::Translate);

  py::converter::registry::push_back(ExceptionTranslator::Convertible,
    ExceptionTranslator::Construct, py::type_id<CJavascriptException>());
}

CJavascriptStackTracePtr CJavascriptStackTrace::GetCurrentStackTrace(
  int frame_limit, v8::StackTrace::StackTraceOptions options)
{
  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;

  v8::Handle<v8::StackTrace> st = v8::StackTrace::CurrentStackTrace(frame_limit, options);

  if (st.IsEmpty()) CJavascriptException::ThrowIf(try_catch);

  return boost::shared_ptr<CJavascriptStackTrace>(new CJavascriptStackTrace(st));
}

CJavascriptStackFramePtr CJavascriptStackTrace::GetFrame(size_t idx) const
{
  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;

  v8::Handle<v8::StackFrame> frame = m_st->GetFrame(idx);

  if (frame.IsEmpty()) CJavascriptException::ThrowIf(try_catch);

  return boost::shared_ptr<CJavascriptStackFrame>(new CJavascriptStackFrame(frame));
}

void CJavascriptStackTrace::Dump(std::ostream& os) const
{
  v8::HandleScope handle_scope;

  v8::TryCatch try_catch;

  std::ostringstream oss;

  for (int i=0; i<m_st->GetFrameCount(); i++)
  {
    v8::Handle<v8::StackFrame> frame = m_st->GetFrame(i);

    v8::String::Utf8Value funcName(frame->GetFunctionName()), scriptName(frame->GetScriptName());

    os << "\tat ";
    
    if (funcName.length())
      os << std::string(*funcName, funcName.length()) << " (";

    if (frame->IsEval())
    {
      os << "(eval)";
    }
    else
    {
      os << std::string(*scriptName, scriptName.length()) << ":" 
          << frame->GetLineNumber() << ":" << frame->GetColumn();
    }

    if (funcName.length())
      os << ")";

    os << std::endl;
  }
}

const std::string CJavascriptStackFrame::GetScriptName() const 
{ 
  v8::HandleScope handle_scope;

  v8::String::Utf8Value name(m_frame->GetScriptName());

  return std::string(*name, name.length());
}
const std::string CJavascriptStackFrame::GetFunctionName() const
{
  v8::HandleScope handle_scope;

  v8::String::Utf8Value name(m_frame->GetFunctionName());

  return std::string(*name, name.length());
}
const std::string CJavascriptException::GetName(void) 
{
  if (m_exc.IsEmpty()) return std::string();

  assert(v8::Context::InContext());

  v8::HandleScope handle_scope;

  v8::String::Utf8Value msg(v8::Handle<v8::String>::Cast(m_exc->ToObject()->Get(v8::String::New("name"))));

  return std::string(*msg, msg.length());
}
const std::string CJavascriptException::GetMessage(void) 
{
  if (m_exc.IsEmpty()) return std::string();

  assert(v8::Context::InContext());

  v8::HandleScope handle_scope;

  v8::String::Utf8Value msg(v8::Handle<v8::String>::Cast(m_exc->ToObject()->Get(v8::String::New("message"))));

  return std::string(*msg, msg.length());
}
const std::string CJavascriptException::GetScriptName(void) 
{
  assert(v8::Context::InContext());

  v8::HandleScope handle_scope;

  if (!m_msg.IsEmpty() && !m_msg->GetScriptResourceName().IsEmpty() &&
      !m_msg->GetScriptResourceName()->IsUndefined())
  {
    v8::String::Utf8Value name(m_msg->GetScriptResourceName());

    return std::string(*name, name.length());
  }

  return std::string();
}
int CJavascriptException::GetLineNumber(void) 
{
  assert(v8::Context::InContext());

  v8::HandleScope handle_scope;

  return m_msg.IsEmpty() ? 1 : m_msg->GetLineNumber();
}
int CJavascriptException::GetStartPosition(void) 
{
  assert(v8::Context::InContext());

  return m_msg.IsEmpty() ? 1 : m_msg->GetStartPosition();
}
int CJavascriptException::GetEndPosition(void)
{
  assert(v8::Context::InContext());

  return m_msg.IsEmpty() ? 1 : m_msg->GetEndPosition();
}
int CJavascriptException::GetStartColumn(void) 
{
  assert(v8::Context::InContext());

  return m_msg.IsEmpty() ? 1 : m_msg->GetStartColumn();
}
int CJavascriptException::GetEndColumn(void) 
{
  assert(v8::Context::InContext());

  return m_msg.IsEmpty() ? 1 : m_msg->GetEndColumn();
}
const std::string CJavascriptException::GetSourceLine(void) 
{
  assert(v8::Context::InContext());

  v8::HandleScope handle_scope;

  if (!m_msg.IsEmpty() && !m_msg->GetSourceLine().IsEmpty() &&
      !m_msg->GetSourceLine()->IsUndefined())
  {
    v8::String::Utf8Value line(m_msg->GetSourceLine());

    return std::string(*line, line.length());
  }

  return std::string();
}
const std::string CJavascriptException::GetStackTrace(void)
{
  assert(v8::Context::InContext());

  v8::HandleScope handle_scope;

  if (!m_stack.IsEmpty())
  {
    v8::String::Utf8Value stack(v8::Handle<v8::String>::Cast(m_stack));

    return std::string(*stack, stack.length());
  }

  return std::string();
}
const std::string CJavascriptException::Extract(v8::TryCatch& try_catch)
{
  assert(v8::Context::InContext());

  v8::HandleScope handle_scope;

  std::ostringstream oss;

  v8::String::Utf8Value msg(try_catch.Exception());

  if (*msg)
    oss << std::string(*msg, msg.length());

  v8::Handle<v8::Message> message = try_catch.Message();

  if (!message.IsEmpty())
  {
    oss << " ( ";

    if (!message->GetScriptResourceName().IsEmpty() &&
        !message->GetScriptResourceName()->IsUndefined())
    {
      v8::String::Utf8Value name(message->GetScriptResourceName());

      oss << std::string(*name, name.length());
    }

    oss << " @ " << message->GetLineNumber() << " : " << message->GetStartColumn() << " ) ";
    
    if (!message->GetSourceLine().IsEmpty() &&
        !message->GetSourceLine()->IsUndefined())
    {
      v8::String::Utf8Value line(message->GetSourceLine());

      oss << " -> " << std::string(*line, line.length());
    }
  }

  return oss.str();
}

static struct {
  const char *name;
  PyObject *type;
} SupportErrors[] = {
  { "RangeError",     ::PyExc_IndexError },
  { "ReferenceError", ::PyExc_ReferenceError },
  { "SyntaxError",    ::PyExc_SyntaxError },
  { "TypeError",      ::PyExc_TypeError }
};

void CJavascriptException::ThrowIf(v8::TryCatch& try_catch)
{
  if (try_catch.HasCaught() && try_catch.CanContinue())
  {
    v8::HandleScope handle_scope;

    PyObject *type = NULL;
    v8::Handle<v8::Value> obj = try_catch.Exception();

    if (obj->IsObject())
    {
      v8::Handle<v8::Object> exc = obj->ToObject();
      v8::Handle<v8::String> name = v8::String::New("name");

      if (exc->Has(name))
      {
        v8::String::Utf8Value s(v8::Handle<v8::String>::Cast(exc->Get(name)));

        for (size_t i=0; i<_countof(SupportErrors); i++)
        {
          if (strnicmp(SupportErrors[i].name, *s, s.length()) == 0)
          {
            type = SupportErrors[i].type;
          }
        }        
      }      
    }

    throw CJavascriptException(try_catch, type);    
  }
}

void ExceptionTranslator::Translate(CJavascriptException const& ex) 
{
  CPythonGIL python_gil;

  if (ex.m_type)
  {
    ::PyErr_SetString(ex.m_type, ex.what());
  }
  else 
  {
    if (!ex.GetException().IsEmpty() && ex.GetException()->IsObject())
    {
      v8::HandleScope handle_scope;

      v8::Handle<v8::Object> obj = ex.GetException()->ToObject();

      v8::Handle<v8::Value> exc_type = obj->GetHiddenValue(v8::String::New("exc_type"));
      v8::Handle<v8::Value> exc_value = obj->GetHiddenValue(v8::String::New("exc_value"));

      if (!exc_type.IsEmpty() && !exc_value.IsEmpty())
      {
        std::auto_ptr<py::object> type(static_cast<py::object *>(v8::Handle<v8::External>::Cast(exc_type)->Value())),
                                  value(static_cast<py::object *>(v8::Handle<v8::External>::Cast(exc_value)->Value()));

        ::PyErr_SetObject(type->ptr(), value->ptr());

        return;
      }
    }

    // Boost::Python doesn't support inherite from Python class,
    // so, just use some workaround to throw our custom exception
    //
    // http://www.language-binding.net/pyplusplus/troubleshooting_guide/exceptions/exceptions.html

    py::object impl(ex);
    py::object clazz = impl.attr("_jsclass");
    py::object err = clazz(impl);

    ::PyErr_SetObject(clazz.ptr(), py::incref(err.ptr()));
  }
}

void *ExceptionTranslator::Convertible(PyObject* obj)
{
  CPythonGIL python_gil;

  if (1 != ::PyObject_IsInstance(obj, ::PyExc_Exception))
    return NULL;

  if (1 != ::PyObject_HasAttrString(obj, "_impl"))
    return NULL;

  py::object err(py::handle<>(py::borrowed(obj)));
  py::object impl = err.attr("_impl");
  py::extract<CJavascriptException> extractor(impl);

  return extractor.check() ? obj : NULL;
}

void ExceptionTranslator::Construct(PyObject* obj, 
  py::converter::rvalue_from_python_stage1_data* data)
{
  CPythonGIL python_gil;

  py::object err(py::handle<>(py::borrowed(obj)));
  py::object impl = err.attr("_impl");

  typedef py::converter::rvalue_from_python_storage<CJavascriptException> storage_t;

  storage_t* the_storage = reinterpret_cast<storage_t*>(data);
  void* memory_chunk = the_storage->storage.bytes;
  CJavascriptException* UNUSED_VAR(cpp_err) =
    new (memory_chunk) CJavascriptException(py::extract<CJavascriptException>(impl));
    
  data->convertible = memory_chunk;
}

void CJavascriptException::PrintCallStack(py::object file)
{  
  CPythonGIL python_gil;

  PyObject *out = file.ptr() == Py_None ? ::PySys_GetObject((char *) "stdout") : file.ptr();

  int fd = ::PyObject_AsFileDescriptor(out);
  
  m_msg->PrintCurrentStackTrace(fdopen(fd, "w+"));
}
