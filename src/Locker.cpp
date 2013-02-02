#include "Locker.h"

#include "V8Internal.h"

bool CLocker::s_preemption = false;

void CLocker::enter(void)
{
    Py_BEGIN_ALLOW_THREADS
    
    m_locker.reset(new v8::Locker(m_isolate.get() ? m_isolate->GetIsolate() : v8i::Isolate::GetDefaultIsolateForLocking()));
    
    Py_END_ALLOW_THREADS
}
void CLocker::leave(void)
{
    Py_BEGIN_ALLOW_THREADS
    
    m_locker.reset();
    
    Py_END_ALLOW_THREADS
}

bool CLocker::IsLocked()
{
  return v8::Locker::IsLocked(v8i::Isolate::GetDefaultIsolateForLocking());
}

void CLocker::StartPreemption(int every_n_ms)
{
  v8::Locker::StartPreemption(every_n_ms);
  
  s_preemption = true;
}
void CLocker::StopPreemption(void)
{
  v8::Locker::StopPreemption();

  s_preemption = false;
}

void CLocker::Expose(void)
{
  py::class_<CLocker, boost::noncopyable>("JSLocker", py::no_init)
    .def(py::init<>())
    .def(py::init<CIsolatePtr>((py::arg("isolate"))))

    .add_static_property("active", &v8::Locker::IsActive, 
                         "whether Locker is being used by this V8 instance.")    

    .add_static_property("locked", &CLocker::IsLocked,
                         "whether or not the locker is locked by the current thread.")    

    .add_static_property("isPreemption", &CLocker::IsPreemption)

    .def("startPreemption", &CLocker::StartPreemption, (py::arg("every_n_ms")=100))    
    .staticmethod("startPreemption")

    .def("stopPreemption", &CLocker::StopPreemption)    
    .staticmethod("stopPreemption")

    .def("entered", &CLocker::entered)

    .def("enter", &CLocker::enter)
    .def("leave", &CLocker::leave)    
    ;

  py::class_<CUnlocker, boost::noncopyable>("JSUnlocker")
    .def("entered", &CUnlocker::entered)

    .def("enter", &CUnlocker::enter)
    .def("leave", &CUnlocker::leave)
    ;
}
