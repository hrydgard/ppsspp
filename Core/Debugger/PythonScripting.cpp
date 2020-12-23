// PPSSPP Python Debugging Interface
// Copyright (c) 2020 Thomas Perl <m@thp.io>.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "ppsspp_config.h"

#include "Common/LogManager.h"

#include "Core/Debugger/PythonScripting.h"

#include "UI/GameInfoCache.h"

#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/Debugger/SymbolMap.h"
#include "Core/MIPS/MIPSDebugInterface.h"

#include <Python.h>

static bool
g_python_inited = false;

static PyObject *
g_on_breakpoint_callback = NULL;

static PyObject *
g_on_frame_callback = NULL;

static PyObject *
g_python_globals = NULL;

void
ppsspp_py_on_breakpoint_callback(u32 addr, void *user_data)
{
	if (g_on_breakpoint_callback && PyCallable_Check(g_on_breakpoint_callback)) {
		PyObject *result = PyObject_CallFunction(g_on_breakpoint_callback, "I", addr);
		if (result == NULL) {
			NOTICE_LOG(SYSTEM, "Error calling Python function, see log");
			PyErr_PrintEx(1);
			PyErr_Clear();
		} else {
			// Unused result for now
			Py_DECREF(result);
		}
	} else {
		NOTICE_LOG(SYSTEM, "Got a breakpoint callback, but no callable object set");
	}
}

PyObject *
ppsspp_py_add_breakpoint(PyObject *self, PyObject *o)
{
	u32 addr = PyLong_AsLong(o);

	CBreakPoints::AddBreakPoint(addr, false);
	CBreakPoints::ChangeBreakPoint(addr, BREAK_ACTION_CALLBACK);

	CBreakPoints::SetBreakpointCallback(ppsspp_py_on_breakpoint_callback, NULL);

	Py_RETURN_NONE;
}

PyObject *
ppsspp_py_remove_breakpoint(PyObject *self, PyObject *o)
{
	u32 addr = PyLong_AsLong(o);

	CBreakPoints::RemoveBreakPoint(addr);

	Py_RETURN_NONE;
}

PyObject *
ppsspp_py_get_breakpoints(PyObject *self)
{
	PyObject *result = PyList_New(0);

	for (const BreakPoint &bp: CBreakPoints::GetBreakpoints()) {
		PyList_Append(result, PyLong_FromLong(bp.addr));
	}

	return result;
}

PyObject *
ppsspp_py_on_breakpoint(PyObject *self, PyObject *callback)
{
	Py_INCREF(callback);

	if (g_on_breakpoint_callback != NULL) {
		Py_DECREF(g_on_breakpoint_callback);
	}

	g_on_breakpoint_callback = callback;

	Py_RETURN_NONE;
}

PyObject *
ppsspp_py_on_frame(PyObject *self, PyObject *callback)
{
	Py_INCREF(callback);

	if (g_on_frame_callback != NULL) {
		Py_DECREF(g_on_frame_callback);
	}

	g_on_frame_callback = callback;

	Py_RETURN_NONE;
}

PyObject *
ppsspp_py_get_reg_value(PyObject *self, PyObject *reg)
{
	return PyLong_FromLong(currentDebugMIPS->GetRegValue(0, PyLong_AsLong(reg)));
}

PyObject *
ppsspp_py_read_memory_u8(PyObject *self, PyObject *addr)
{
	return PyLong_FromLong(Memory::ReadUnchecked_U8(PyLong_AsLong(addr)));
}

PyObject *
ppsspp_py_write_memory_u8(PyObject *self, PyObject *args)
{
	unsigned long addr;
	unsigned long value;
	if (!PyArg_ParseTuple(args, "kk", &addr, &value)) {
	    return NULL;
	}

	Memory::WriteUnchecked_U8(addr, value & 0xFF);

	Py_RETURN_NONE;
}

PyObject *
ppsspp_py_get_game_id(PyObject *self)
{
	std::string gamePath = PSP_CoreParameter().fileToStart;
	std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(nullptr, gamePath, 0);

	return PyUnicode_FromString(ginfo->id.c_str());
}

PyObject *
ppsspp_py_get_memory_pointer(PyObject *self)
{
	return PyLong_FromVoidPtr(Memory::base);
}

PyObject *
ppsspp_py_get_ram_memory_view(PyObject *self)
{
	return PyMemoryView_FromMemory((char *)Memory::base + 0x08000000, 32ul * 1024ul * 1024ul, PyBUF_READ);
}

PyObject *
ppsspp_py_get_all_symbols(PyObject *self)
{
	std::vector<SymbolEntry> symbols = g_symbolMap->GetAllSymbols(ST_ALL);

	PyObject *result = PyList_New(symbols.size());

	for (int i=0; i<symbols.size(); ++i) {
		const SymbolEntry &entry = symbols[i];
		PyList_SetItem(result, i, Py_BuildValue("(sii)", entry.name.c_str(), entry.address, entry.size));
	}

	return result;
}

static PyMethodDef
PPSSPPMethods[] = {
	{"add_breakpoint", ppsspp_py_add_breakpoint, METH_O, "Add a breakpoint"},
	{"remove_breakpoint", ppsspp_py_remove_breakpoint, METH_O, "Remove a breakpoint"},
	{"get_breakpoints", (PyCFunction)ppsspp_py_get_breakpoints, METH_NOARGS, "Get breakpoints"},
	{"on_breakpoint", ppsspp_py_on_breakpoint, METH_O, "Set a function to be called when a breakpoint is hit"},
	{"on_frame", ppsspp_py_on_frame, METH_O, "Set a function to be called in the mainloop"},
	{"get_reg_value", ppsspp_py_get_reg_value, METH_O, "Read the content of a register"},
	{"read_memory_u8", ppsspp_py_read_memory_u8, METH_O, "Read a byte from memory"},
	{"write_memory_u8", ppsspp_py_write_memory_u8, METH_VARARGS, "Write a byte to memory"},
	{"get_game_id", (PyCFunction)ppsspp_py_get_game_id, METH_NOARGS, "Get the game ID (e.g. UCXX12345) as string"},
	{"get_memory_pointer", (PyCFunction)ppsspp_py_get_memory_pointer, METH_NOARGS, "Get a pointer to the PSP memory"},
	{"get_ram_memory_view", (PyCFunction)ppsspp_py_get_ram_memory_view, METH_NOARGS, "Get a view of the PSP RAM"},
	{"get_all_symbols", (PyCFunction)ppsspp_py_get_all_symbols, METH_NOARGS, "Get all symbols from the symbol map"},
	{NULL, NULL, 0, NULL},
};

static PyModuleDef
PPSSPPModule = {
	PyModuleDef_HEAD_INIT,
	"ppsspp",
	NULL,
	-1,
	PPSSPPMethods,
};

PyMODINIT_FUNC
ppsspp_py_init()
{
	PyObject *ppsspp_module = PyModule_Create(&PPSSPPModule);

	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_ZERO", MIPS_REG_ZERO);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_COMPILER_SCRATCH", MIPS_REG_COMPILER_SCRATCH);

	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_V0", MIPS_REG_V0);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_V1", MIPS_REG_V1);

	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_A0", MIPS_REG_A0);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_A1", MIPS_REG_A1);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_A2", MIPS_REG_A2);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_A3", MIPS_REG_A3);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_A4", MIPS_REG_A4);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_A5", MIPS_REG_A5);

	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_T0", MIPS_REG_T0);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_T1", MIPS_REG_T1);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_T2", MIPS_REG_T2);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_T3", MIPS_REG_T3);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_T4", MIPS_REG_T4);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_T5", MIPS_REG_T5);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_T6", MIPS_REG_T6);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_T7", MIPS_REG_T7);

	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_S0", MIPS_REG_S0);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_S1", MIPS_REG_S1);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_S2", MIPS_REG_S2);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_S3", MIPS_REG_S3);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_S4", MIPS_REG_S4);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_S5", MIPS_REG_S5);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_S6", MIPS_REG_S6);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_S7", MIPS_REG_S7);

	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_T8", MIPS_REG_T8);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_T9", MIPS_REG_T9);

	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_K0", MIPS_REG_K0);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_K1", MIPS_REG_K1);

	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_GP", MIPS_REG_GP);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_SP", MIPS_REG_SP);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_FP", MIPS_REG_FP);
	PyModule_AddIntConstant(ppsspp_module, "MIPS_REG_RA", MIPS_REG_RA);

	PyModule_AddIntConstant(ppsspp_module, "default_load_address", 0x08804000);

	// http://daifukkat.su/docs/psptek/#memmap
	PyModule_AddIntConstant(ppsspp_module, "scratchpad_address",   0x00010000);
	PyModule_AddIntConstant(ppsspp_module, "vram_address",         0x04000000);
	PyModule_AddIntConstant(ppsspp_module, "ram_address",          0x08000000);

	return ppsspp_module;
}

void
PPSSPPPythonScripting::init()
{
	if (g_python_inited) {
		return;
	}

	NOTICE_LOG(SYSTEM, "Initing Python Scripting");

	PyImport_AppendInittab("ppsspp", ppsspp_py_init);
	Py_Initialize();
	PyEval_InitThreads();

	g_python_globals = PyDict_New();
	PyDict_SetItemString(g_python_globals, "ppsspp", PyImport_ImportModule("ppsspp"));

	g_python_inited = true;
}

void PPSSPPPythonScripting::run_file(const std::string &filename)
{
	init();

	FILE *fp = fopen(filename.c_str(), "r");
	PyObject *result = PyRun_File(fp, filename.c_str(), Py_file_input, g_python_globals, g_python_globals);
	if (result == NULL) {
		PyErr_PrintEx(1);
		PyErr_Clear();
	} else {
		Py_DECREF(result);
	}
	fclose(fp);
}

std::string PPSSPPPythonScripting::eval(const std::string &expr)
{
	init();

	PyObject *result = PyRun_String(expr.c_str(), Py_eval_input, g_python_globals, g_python_globals);
	if (result) {
		PyObject *repr = PyObject_Repr(result);
		std::string repr_str = PyUnicode_AsUTF8(repr);
		Py_DECREF(repr);
		Py_DECREF(result);
		return repr_str;
	} else {
		PyErr_PrintEx(1);
		PyErr_Clear();
		return "ERROR (see logs)";
	}
}

void
PPSSPPPythonScripting::deinit()
{
	if (g_python_inited) {
		Py_FinalizeEx();
		g_python_inited = false;
	}
}

void
PPSSPPPythonScripting::frame_hook()
{
	if (g_python_inited && g_on_frame_callback && PyCallable_Check(g_on_frame_callback)) {
		PyObject *result = PyObject_CallFunction(g_on_frame_callback, NULL);
		if (result == NULL) {
			NOTICE_LOG(SYSTEM, "Error calling Python function, see log");
			PyErr_PrintEx(1);
			PyErr_Clear();
		} else {
			// Unused result for now
			Py_DECREF(result);
		}
	}
}
