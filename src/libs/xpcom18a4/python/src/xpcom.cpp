/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Python XPCOM language bindings.
 *
 * The Initial Developer of the Original Code is
 * ActiveState Tool Corp.
 * Portions created by the Initial Developer are Copyright (C) 2000
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mark Hammond <mhammond@skippinet.com.au> (original author)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

//
// This code is part of the XPCOM extensions for Python.
//
// Written May 2000 by Mark Hammond.
//
// Based heavily on the Python COM support, which is
// (c) Mark Hammond and Greg Stein.
//
// (c) 2000, ActiveState corp.

#include "PyXPCOM_std.h"
#include "nsIThread.h"
#include "nsXPCOM.h"
#include "nsISupportsPrimitives.h"
#include "nsIModule.h"
#include "nsIFile.h"
#include "nsILocalFile.h"
#include "nsIComponentRegistrar.h"
#include "nsIComponentManagerObsolete.h"

#ifdef XP_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "windows.h"
#endif

#include "nsIEventQueue.h"
#include "nsIProxyObjectManager.h"

PYXPCOM_EXPORT PyObject *PyXPCOM_Error = NULL;
extern PRInt32 _PyXPCOM_GetGatewayCount(void);
extern PRInt32 _PyXPCOM_GetInterfaceCount(void);

extern void AddDefaultGateway(PyObject *instance, nsISupports *gateway);

#define LOADER_LINKS_WITH_PYTHON

#ifndef PYXPCOM_USE_PYGILSTATE
extern void PyXPCOM_InterpreterState_Ensure();
#endif

PyXPCOM_INTERFACE_DEFINE(Py_nsIComponentManager, nsIComponentManager, PyMethods_IComponentManager)
PyXPCOM_INTERFACE_DEFINE(Py_nsIInterfaceInfoManager, nsIInterfaceInfoManager, PyMethods_IInterfaceInfoManager)
PyXPCOM_INTERFACE_DEFINE(Py_nsIEnumerator, nsIEnumerator, PyMethods_IEnumerator)
PyXPCOM_INTERFACE_DEFINE(Py_nsISimpleEnumerator, nsISimpleEnumerator, PyMethods_ISimpleEnumerator)
PyXPCOM_INTERFACE_DEFINE(Py_nsIInterfaceInfo, nsIInterfaceInfo, PyMethods_IInterfaceInfo)
PyXPCOM_INTERFACE_DEFINE(Py_nsIInputStream, nsIInputStream, PyMethods_IInputStream)
PyXPCOM_INTERFACE_DEFINE(Py_nsIClassInfo, nsIClassInfo, PyMethods_IClassInfo)
PyXPCOM_INTERFACE_DEFINE(Py_nsIVariant, nsIVariant, PyMethods_IVariant)
// deprecated, but retained for backward compatibility:
PyXPCOM_INTERFACE_DEFINE(Py_nsIComponentManagerObsolete, nsIComponentManagerObsolete, PyMethods_IComponentManagerObsolete)

////////////////////////////////////////////////////////////
// This is the main entry point called by the Python component
// loader.
extern "C" NS_EXPORT nsresult PyXPCOM_NSGetModule(nsIComponentManager *servMgr,
                                          nsIFile* location,
                                          nsIModule** result)
{
	NS_PRECONDITION(result!=NULL, "null result pointer in PyXPCOM_NSGetModule!");
	NS_PRECONDITION(location!=NULL, "null nsIFile pointer in PyXPCOM_NSGetModule!");
	NS_PRECONDITION(servMgr!=NULL, "null servMgr pointer in PyXPCOM_NSGetModule!");
#ifndef LOADER_LINKS_WITH_PYTHON
	if (!Py_IsInitialized()) {
		Py_Initialize();
		if (!Py_IsInitialized()) {
			PyXPCOM_LogError("Python initialization failed!\n");
			return NS_ERROR_FAILURE;
		}
		PyEval_InitThreads();
#ifndef PYXPCOM_USE_PYGILSTATE
		PyXPCOM_InterpreterState_Ensure();
#endif
		PyEval_SaveThread();
	}
#endif // LOADER_LINKS_WITH_PYTHON	
	CEnterLeavePython _celp;
	PyObject *func = NULL;
	PyObject *obServMgr = NULL;
	PyObject *obLocation = NULL;
	PyObject *wrap_ret = NULL;
	PyObject *args = NULL;
	PyObject *mod = PyImport_ImportModule("xpcom.server");
	if (!mod) goto done;
	func = PyObject_GetAttrString(mod, "NS_GetModule");
	if (func==NULL) goto done;
	obServMgr = Py_nsISupports::PyObjectFromInterface(servMgr, NS_GET_IID(nsIComponentManager), PR_TRUE);
	if (obServMgr==NULL) goto done;
	obLocation = Py_nsISupports::PyObjectFromInterface(location, NS_GET_IID(nsIFile), PR_TRUE);
	if (obLocation==NULL) goto done;
	args = Py_BuildValue("OO", obServMgr, obLocation);
	if (args==NULL) goto done;
	wrap_ret = PyEval_CallObject(func, args);
	if (wrap_ret==NULL) goto done;
	Py_nsISupports::InterfaceFromPyObject(wrap_ret, NS_GET_IID(nsIModule), (nsISupports **)result, PR_FALSE, PR_FALSE);
done:
	nsresult nr = NS_OK;
	if (PyErr_Occurred()) {
		PyXPCOM_LogError("Obtaining the module object from Python failed.\n");
		nr = PyXPCOM_SetCOMErrorFromPyException();
	}
	Py_XDECREF(func);
	Py_XDECREF(obServMgr);
	Py_XDECREF(obLocation);
	Py_XDECREF(wrap_ret);
	Py_XDECREF(mod);
	Py_XDECREF(args);
	return nr;
}

// "boot-strap" methods - interfaces we need to get the base
// interface support!

/* deprecated, included for backward compatibility */
static PyObject *
PyXPCOMMethod_NS_GetGlobalComponentManager(PyObject *self, PyObject *args)
{
	if (PyErr_Warn(PyExc_DeprecationWarning, "Use GetComponentManager instead") < 0)
	       return NULL;
	if (!PyArg_ParseTuple(args, ""))
		return NULL;
	nsIComponentManager* cm;
	nsresult rv;
	Py_BEGIN_ALLOW_THREADS;
	rv = NS_GetComponentManager(&cm);
	Py_END_ALLOW_THREADS;
	if ( NS_FAILED(rv) )
		return PyXPCOM_BuildPyException(rv);

	nsCOMPtr<nsIComponentManagerObsolete> ocm(do_QueryInterface(cm, &rv));
	if ( NS_FAILED(rv) )
		return PyXPCOM_BuildPyException(rv);

	return Py_nsISupports::PyObjectFromInterface(cm, NS_GET_IID(nsIComponentManagerObsolete), PR_FALSE, PR_FALSE);
}

static PyObject *
PyXPCOMMethod_GetComponentManager(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ""))
		return NULL;
	nsIComponentManager* cm;
	nsresult rv;
	Py_BEGIN_ALLOW_THREADS;
	rv = NS_GetComponentManager(&cm);
	Py_END_ALLOW_THREADS;
	if ( NS_FAILED(rv) )
		return PyXPCOM_BuildPyException(rv);

	return Py_nsISupports::PyObjectFromInterface(cm, NS_GET_IID(nsIComponentManager), PR_FALSE, PR_FALSE);
}

static PyObject *
PyXPCOMMethod_GetServiceManager(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ""))
		return NULL;
	nsIServiceManager* sm;
	nsresult rv;
	Py_BEGIN_ALLOW_THREADS;
	rv = NS_GetServiceManager(&sm);
	Py_END_ALLOW_THREADS;
	if ( NS_FAILED(rv) )
		return PyXPCOM_BuildPyException(rv);

	// Return a type based on the IID.
	return Py_nsISupports::PyObjectFromInterface(sm, NS_GET_IID(nsIServiceManager), PR_FALSE);
}

/* deprecated, included for backward compatibility */
static PyObject *
PyXPCOMMethod_GetGlobalServiceManager(PyObject *self, PyObject *args)
{
	if (PyErr_Warn(PyExc_DeprecationWarning, "Use GetServiceManager instead") < 0)
		return NULL;

	return PyXPCOMMethod_GetComponentManager(self, args);
}
		
static PyObject *
PyXPCOMMethod_XPTI_GetInterfaceInfoManager(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ""))
		return NULL;
	nsCOMPtr<nsIInterfaceInfoManager> im(do_GetService(
	                NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID));
	if ( im == nsnull )
		return PyXPCOM_BuildPyException(NS_ERROR_FAILURE);

	/* Return a type based on the IID (with no extra ref) */
	// Can not auto-wrap the interface info manager as it is critical to
	// building the support we need for autowrap.
	return Py_nsISupports::PyObjectFromInterface(im, NS_GET_IID(nsIInterfaceInfoManager), PR_FALSE, PR_FALSE);
}

static PyObject *
PyXPCOMMethod_XPTC_InvokeByIndex(PyObject *self, PyObject *args)
{
	PyObject *obIS, *obParams;
	nsCOMPtr<nsISupports> pis;
	int index;

	// We no longer rely on PyErr_Occurred() for our error state,
	// but keeping this assertion can't hurt - it should still always be true!
	NS_WARN_IF_FALSE(!PyErr_Occurred(), "Should be no pending Python error!");

	if (!PyArg_ParseTuple(args, "OiO", &obIS, &index, &obParams))
		return NULL;

	// Ack!  We must ask for the "native" interface supported by
	// the object, not specifically nsISupports, else we may not
	// back the same pointer (eg, Python, following identity rules,
	// will return the "original" gateway when QI'd for nsISupports)
	if (!Py_nsISupports::InterfaceFromPyObject(
			obIS, 
			Py_nsIID_NULL, 
			getter_AddRefs(pis), 
			PR_FALSE))
		return NULL;

	PyXPCOM_InterfaceVariantHelper arg_helper;
	if (!arg_helper.Init(obParams))
		return NULL;

	if (!arg_helper.FillArray())
		return NULL;

	nsresult r;
	Py_BEGIN_ALLOW_THREADS;
	r = XPTC_InvokeByIndex(pis, index, arg_helper.m_num_array, arg_helper.m_var_array);
	Py_END_ALLOW_THREADS;
	if ( NS_FAILED(r) )
		return PyXPCOM_BuildPyException(r);

	return arg_helper.MakePythonResult();
}

static PyObject *
PyXPCOMMethod_WrapObject(PyObject *self, PyObject *args)
{
	PyObject *ob, *obIID;
	int bWrapClient = 1;
	if (!PyArg_ParseTuple(args, "OO|i", &ob, &obIID, &bWrapClient))
		return NULL;

	nsIID	iid;
	if (!Py_nsIID::IIDFromPyObject(obIID, &iid))
		return NULL;

	nsISupports *ret = NULL;
	nsresult r = PyXPCOM_XPTStub::CreateNew(ob, iid, (void **)&ret);
	if ( NS_FAILED(r) )
		return PyXPCOM_BuildPyException(r);

	// _ALL_ wrapped objects are associated with a weak-ref
	// to their "main" instance.
	AddDefaultGateway(ob, ret); // inject a weak reference to myself into the instance.

	// Now wrap it in an interface.
	return Py_nsISupports::PyObjectFromInterface(ret, iid, PR_FALSE, bWrapClient);
}

static PyObject *
PyXPCOMMethod_UnwrapObject(PyObject *self, PyObject *args)
{
	PyObject *ob;
	if (!PyArg_ParseTuple(args, "O", &ob))
		return NULL;

	nsISupports *uob = NULL;
	nsIInternalPython *iob = NULL;
	PyObject *ret = NULL;
	if (!Py_nsISupports::InterfaceFromPyObject(ob, 
				NS_GET_IID(nsISupports), 
				&uob, 
				PR_FALSE))
		goto done;
	if (NS_FAILED(uob->QueryInterface(NS_GET_IID(nsIInternalPython), reinterpret_cast<void **>(&iob)))) {
		PyErr_SetString(PyExc_ValueError, "This XPCOM object is not implemented by Python");
		goto done;
	}
	ret = iob->UnwrapPythonObject();
done:
	Py_BEGIN_ALLOW_THREADS;
	NS_IF_RELEASE(uob);
	NS_IF_RELEASE(iob);
	Py_END_ALLOW_THREADS;
	return ret;
}

// @pymethod int|pythoncom|_GetInterfaceCount|Retrieves the number of interface objects currently in existance
static PyObject *
PyXPCOMMethod_GetInterfaceCount(PyObject *self, PyObject *args)
{
	if (!PyArg_ParseTuple(args, ":_GetInterfaceCount"))
		return NULL;
	return PyInt_FromLong(_PyXPCOM_GetInterfaceCount());
	// @comm If is occasionally a good idea to call this function before your Python program
	// terminates.  If this function returns non-zero, then you still have PythonCOM objects
	// alive in your program (possibly in global variables).
}

// @pymethod int|pythoncom|_GetGatewayCount|Retrieves the number of gateway objects currently in existance
static PyObject *
PyXPCOMMethod_GetGatewayCount(PyObject *self, PyObject *args)
{
	// @comm This is the number of Python object that implement COM servers which
	// are still alive (ie, serving a client).  The only way to reduce this count
	// is to have the process which uses these PythonCOM servers release its references.
	if (!PyArg_ParseTuple(args, ":_GetGatewayCount"))
		return NULL;
	return PyInt_FromLong(_PyXPCOM_GetGatewayCount());
}

static PyObject *
PyXPCOMMethod_NS_ShutdownXPCOM(PyObject *self, PyObject *args)
{
	// @comm This is the number of Python object that implement COM servers which
	// are still alive (ie, serving a client).  The only way to reduce this count
	// is to have the process which uses these PythonCOM servers release its references.
	if (!PyArg_ParseTuple(args, ":NS_ShutdownXPCOM"))
		return NULL;
	nsresult nr;
	Py_BEGIN_ALLOW_THREADS;
	nr = NS_ShutdownXPCOM(nsnull);
	Py_END_ALLOW_THREADS;

	// Dont raise an exception - as we are probably shutting down
	// and dont really case - just return the status
	return PyInt_FromLong(nr);
}

static NS_DEFINE_CID(kProxyObjectManagerCID, NS_PROXYEVENT_MANAGER_CID);

// A hack to work around their magic constants!
static PyObject *
PyXPCOMMethod_GetProxyForObject(PyObject *self, PyObject *args)
{
	PyObject *obQueue, *obIID, *obOb;
	int flags;
	if (!PyArg_ParseTuple(args, "OOOi", &obQueue, &obIID, &obOb, &flags))
		return NULL;
	nsIID iid;
	if (!Py_nsIID::IIDFromPyObject(obIID, &iid))
		return NULL;
	nsCOMPtr<nsISupports> pob;
	if (!Py_nsISupports::InterfaceFromPyObject(obOb, iid, getter_AddRefs(pob), PR_FALSE))
		return NULL;
	nsIEventQueue *pQueue = NULL;
	nsIEventQueue *pQueueRelease = NULL;

	if (PyInt_Check(obQueue)) {
		pQueue = (nsIEventQueue *)PyInt_AsLong(obQueue);
	} else {
		if (!Py_nsISupports::InterfaceFromPyObject(obQueue, NS_GET_IID(nsIEventQueue), (nsISupports **)&pQueue, PR_TRUE))
			return NULL;
		pQueueRelease = pQueue;
	}

	nsresult rv_proxy;
	nsISupports *presult = nsnull;
	Py_BEGIN_ALLOW_THREADS;
	nsCOMPtr<nsIProxyObjectManager> proxyMgr = 
	         do_GetService(kProxyObjectManagerCID, &rv_proxy);

	if ( NS_SUCCEEDED(rv_proxy) ) {
		rv_proxy = proxyMgr->GetProxyForObject(pQueue,
				iid,
				pob,
				flags,
				(void **)&presult);
	}
	if (pQueueRelease)
		pQueueRelease->Release();
	Py_END_ALLOW_THREADS;

	PyObject *result;
	if (NS_SUCCEEDED(rv_proxy) ) {
		result = Py_nsISupports::PyObjectFromInterface(presult, iid, PR_FALSE);
	} else {
		result = PyXPCOM_BuildPyException(rv_proxy);
	}
	return result;
}

PyObject *PyGetSpecialDirectory(PyObject *self, PyObject *args)
{
	char *dirname;
	if (!PyArg_ParseTuple(args, "s:GetSpecialDirectory", &dirname))
		return NULL;
	nsIFile *file = NULL;
	nsresult r = NS_GetSpecialDirectory(dirname, &file);
	if ( NS_FAILED(r) )
		return PyXPCOM_BuildPyException(r);
	// returned object swallows our reference.
	return Py_nsISupports::PyObjectFromInterface(file, NS_GET_IID(nsIFile), PR_FALSE);
}

PyObject *AllocateBuffer(PyObject *self, PyObject *args)
{
	int bufSize;
	if (!PyArg_ParseTuple(args, "i", &bufSize))
		return NULL;
	return PyBuffer_New(bufSize);
}

PyObject *LogWarning(PyObject *self, PyObject *args)
{
	char *msg;
	if (!PyArg_ParseTuple(args, "s", &msg))
		return NULL;
	PyXPCOM_LogWarning("%s", msg);
	Py_INCREF(Py_None);
	return Py_None;
}

PyObject *LogError(PyObject *self, PyObject *args)
{
	char *msg;
	if (!PyArg_ParseTuple(args, "s", &msg))
		return NULL;
	PyXPCOM_LogError("%s", msg);
	Py_INCREF(Py_None);
	return Py_None;
}

extern PyObject *PyXPCOMMethod_IID(PyObject *self, PyObject *args);

static struct PyMethodDef xpcom_methods[]=
{
	{"GetComponentManager", PyXPCOMMethod_GetComponentManager, 1},
	{"NS_GetGlobalComponentManager", PyXPCOMMethod_NS_GetGlobalComponentManager, 1}, // deprecated
	{"XPTI_GetInterfaceInfoManager", PyXPCOMMethod_XPTI_GetInterfaceInfoManager, 1},
	{"XPTC_InvokeByIndex", PyXPCOMMethod_XPTC_InvokeByIndex, 1},
	{"GetServiceManager", PyXPCOMMethod_GetServiceManager, 1},
	{"GetGlobalServiceManager", PyXPCOMMethod_GetGlobalServiceManager, 1}, // deprecated
	{"IID", PyXPCOMMethod_IID, 1}, // IID is wrong - deprecated - not just IID, but CID, etc. 
	{"ID", PyXPCOMMethod_IID, 1}, // This is the official name.
	{"NS_ShutdownXPCOM", PyXPCOMMethod_NS_ShutdownXPCOM, 1},
	{"WrapObject", PyXPCOMMethod_WrapObject, 1},
	{"UnwrapObject", PyXPCOMMethod_UnwrapObject, 1},
	{"_GetInterfaceCount", PyXPCOMMethod_GetInterfaceCount, 1},
	{"_GetGatewayCount", PyXPCOMMethod_GetGatewayCount, 1},
	{"getProxyForObject", PyXPCOMMethod_GetProxyForObject, 1},
	{"GetProxyForObject", PyXPCOMMethod_GetProxyForObject, 1},
	{"GetSpecialDirectory", PyGetSpecialDirectory, 1},
	{"AllocateBuffer", AllocateBuffer, 1},
	{"LogWarning", LogWarning, 1},
	{"LogError", LogError, 1},
	{ NULL }
};

////////////////////////////////////////////////////////////
// Other helpers/global functions.
//
PRBool PyXPCOM_Globals_Ensure()
{
	PRBool rc = PR_TRUE;

#ifndef PYXPCOM_USE_PYGILSTATE
	PyXPCOM_InterpreterState_Ensure();
#endif

	// The exception object - we load it from .py code!
	if (PyXPCOM_Error == NULL) {
		rc = PR_FALSE;
		PyObject *mod = NULL;

		mod = PyImport_ImportModule("xpcom");
		if (mod!=NULL) {
			PyXPCOM_Error = PyObject_GetAttrString(mod, "Exception");
			Py_DECREF(mod);
		}
		rc = (PyXPCOM_Error != NULL);
	}
	if (!rc)
		return rc;

	static PRBool bHaveInitXPCOM = PR_FALSE;
	if (!bHaveInitXPCOM) {
		nsCOMPtr<nsIThread> thread_check;
		// xpcom appears to assert if already initialized
		// Is there an official way to determine this?
		if (NS_FAILED(nsIThread::GetMainThread(getter_AddRefs(thread_check)))) {
			// not already initialized.
#ifdef XP_WIN
			// On Windows, we need to locate the Mozilla bin
			// directory.  This by using locating a Moz DLL we depend
			// on, and assume it lives in that bin dir.  Different
			// moz build types (eg, xulrunner, suite) package
			// XPCOM itself differently - but all appear to require
			// nspr4.dll - so this is what we use.
			char landmark[MAX_PATH+1];
			HMODULE hmod = GetModuleHandle("nspr4.dll");
			if (hmod==NULL) {
				PyErr_SetString(PyExc_RuntimeError, "We dont appear to be linked against nspr4.dll.");
				return PR_FALSE;
			}
			GetModuleFileName(hmod, landmark, sizeof(landmark)/sizeof(landmark[0]));
			char *end = landmark + (strlen(landmark)-1);
			while (end > landmark && *end != '\\')
				end--;
			if (end > landmark) *end = '\0';

			nsCOMPtr<nsILocalFile> ns_bin_dir;
			NS_ConvertASCIItoUCS2 strLandmark(landmark);
			NS_NewLocalFile(strLandmark, PR_FALSE, getter_AddRefs(ns_bin_dir));
			nsresult rv = NS_InitXPCOM2(nsnull, ns_bin_dir, nsnull);
#else
			// Elsewhere, Mozilla can find it itself (we hope!)
			nsresult rv = NS_InitXPCOM2(nsnull, nsnull, nsnull);
#endif // XP_WIN
			if (NS_FAILED(rv)) {
				PyErr_SetString(PyExc_RuntimeError, "The XPCOM subsystem could not be initialized");
				return PR_FALSE;
			}
		}
		// Even if xpcom was already init, we want to flag it as init!
		bHaveInitXPCOM = PR_TRUE;
	}
	return rc;
}


#define REGISTER_IID(t) { \
	PyObject *iid_ob = Py_nsIID::PyObjectFromIID(NS_GET_IID(t)); \
	PyDict_SetItemString(dict, "IID_"#t, iid_ob); \
	Py_DECREF(iid_ob); \
	}

#define REGISTER_INT(val) { \
	PyObject *ob = PyInt_FromLong(val); \
	PyDict_SetItemString(dict, #val, ob); \
	Py_DECREF(ob); \
	}


////////////////////////////////////////////////////////////
// The module init code.
//
extern "C" NS_EXPORT
void 
init_xpcom() {
	PyObject *oModule;

	// ensure the framework has valid state to work with.
	if (!PyXPCOM_Globals_Ensure())
		return;

	// Must force Python to start using thread locks
	PyEval_InitThreads();

	// Create the module and add the functions
	oModule = Py_InitModule("_xpcom", xpcom_methods);

	PyObject *dict = PyModule_GetDict(oModule);
	PyObject *pycom_Error = PyXPCOM_Error;
	if (pycom_Error == NULL || PyDict_SetItemString(dict, "error", pycom_Error) != 0)
	{
		PyErr_SetString(PyExc_MemoryError, "can't define error");
		return;
	}
	PyDict_SetItemString(dict, "IIDType", (PyObject *)&Py_nsIID::type);

	// register our entry point.
	PyObject *obFuncPtr = PyLong_FromVoidPtr((void *)&PyXPCOM_NSGetModule);
	if (obFuncPtr)
		PyDict_SetItemString(dict, 
		                             "_NSGetModule_FuncPtr", 
		                             obFuncPtr);
	Py_XDECREF(obFuncPtr);

	REGISTER_IID(nsISupports);
	REGISTER_IID(nsISupportsCString);
	REGISTER_IID(nsIModule);
	REGISTER_IID(nsIFactory);
	REGISTER_IID(nsIWeakReference);
	REGISTER_IID(nsISupportsWeakReference);
	REGISTER_IID(nsIClassInfo);
	REGISTER_IID(nsIServiceManager);
	REGISTER_IID(nsIComponentRegistrar);
	// Register our custom interfaces.

	Py_nsISupports::InitType();
	Py_nsIComponentManager::InitType(dict);
	Py_nsIInterfaceInfoManager::InitType(dict);
	Py_nsIEnumerator::InitType(dict);
	Py_nsISimpleEnumerator::InitType(dict);
	Py_nsIInterfaceInfo::InitType(dict);
	Py_nsIInputStream::InitType(dict);
	Py_nsIClassInfo::InitType(dict);
	Py_nsIVariant::InitType(dict);
	// for backward compatibility:
	Py_nsIComponentManagerObsolete::InitType(dict);

    // We have special support for proxies - may as well add their constants!
    REGISTER_INT(PROXY_SYNC);
    REGISTER_INT(PROXY_ASYNC);
    REGISTER_INT(PROXY_ALWAYS);
}
