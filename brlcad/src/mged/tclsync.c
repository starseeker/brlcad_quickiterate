#include <tcl.h>
#include <string.h>
#include <stdio.h>

/* =========================
 * Utility: Check if array
 * ========================= */
static int IsArray(Tcl_Interp *interp, const char *fqName) {
    Tcl_Obj *cmd = Tcl_ObjPrintf("array exists %s", fqName);
    int rc = Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL);
    Tcl_DecrRefCount(cmd);

    if (rc != TCL_OK) return 0;
    return atoi(Tcl_GetStringResult(interp));
}

/* =========================
 * Append variable (scalar/array)
 * ========================= */
static void AppendVar(Tcl_Interp *interp, Tcl_Obj *script,
                      const char *varName, const char *nsPrefix)
{
    char fqName[1024];

    if (nsPrefix && strcmp(nsPrefix, "::") != 0) {
        snprintf(fqName, sizeof(fqName), "%s::%s", nsPrefix, varName);
    } else {
        snprintf(fqName, sizeof(fqName), "%s", varName);
    }

    /* ARRAY */
    if (IsArray(interp, fqName)) {
        Tcl_Obj *cmd = Tcl_ObjPrintf("array get %s", fqName);

        if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL) == TCL_OK) {
            Tcl_Obj *list = Tcl_GetObjResult(interp);

            Tcl_Obj *line = Tcl_NewStringObj("", -1);

            if (nsPrefix && strcmp(nsPrefix, "::") != 0) {
                Tcl_AppendPrintfToObj(line,
                    "namespace eval %s { array set %s ",
                    nsPrefix, varName);
                Tcl_AppendObjToObj(line, list);
                Tcl_AppendToObj(line, " }\n", -1);
            } else {
                Tcl_AppendPrintfToObj(line,
                    "array set %s ", varName);
                Tcl_AppendObjToObj(line, list);
                Tcl_AppendToObj(line, "\n", -1);
            }

            Tcl_AppendObjToObj(script, line);
        }

        Tcl_DecrRefCount(cmd);
        return;
    }

    /* SCALAR */
    Tcl_Obj *val = Tcl_GetVar2Ex(interp, fqName, NULL, TCL_GLOBAL_ONLY);
    if (!val) return;

    if (nsPrefix && strcmp(nsPrefix, "::") != 0) {
        Tcl_Obj *line = Tcl_NewStringObj("", -1);

        Tcl_AppendPrintfToObj(line,
            "namespace eval %s { variable %s ",
            nsPrefix, varName);

        Tcl_AppendObjToObj(line, val);
        Tcl_AppendToObj(line, " }\n", -1);

        Tcl_AppendObjToObj(script, line);
    } else {
        Tcl_Obj *cmd = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj("set", -1));
        Tcl_ListObjAppendElement(NULL, cmd, Tcl_NewStringObj(varName, -1));
        Tcl_ListObjAppendElement(NULL, cmd, val);

        Tcl_AppendObjToObj(script, cmd);
        Tcl_AppendToObj(script, "\n", -1);
    }
}

/* =========================
 * Append global variables
 * ========================= */
static int AppendGlobals(Tcl_Interp *interp, Tcl_Obj *script) {
    if (Tcl_Eval(interp, "info globals") != TCL_OK)
        return TCL_ERROR;

    Tcl_Obj *list = Tcl_GetObjResult(interp);

    int count;
    Tcl_Obj **elems;

    if (Tcl_ListObjGetElements(interp, list, &count, &elems) != TCL_OK)
        return TCL_ERROR;

    for (int i = 0; i < count; i++) {
        const char *name = Tcl_GetString(elems[i]);

        if (name[0] == '_') continue; /* skip internals */

        AppendVar(interp, script, name, NULL);
    }

    return TCL_OK;
}

/* =========================
 * Recursive namespace vars
 * ========================= */
static int AppendNamespaceVarsRec(Tcl_Interp *interp,
                                 Tcl_Obj *script,
                                 const char *nsName)
{
    Tcl_Obj *cmd = Tcl_ObjPrintf("info vars %s::*", nsName);

    if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(cmd);

    Tcl_Obj *vars = Tcl_GetObjResult(interp);

    int count;
    Tcl_Obj **elems;

    if (Tcl_ListObjGetElements(interp, vars, &count, &elems) == TCL_OK) {
        for (int i = 0; i < count; i++) {
            const char *fq = Tcl_GetString(elems[i]);

            const char *tail = strrchr(fq, ':');
            if (!tail) continue;
            tail++;

            AppendVar(interp, script, tail, nsName);
        }
    }

    /* recurse children */
    cmd = Tcl_ObjPrintf("namespace children %s", nsName);

    if (Tcl_EvalObjEx(interp, cmd, TCL_EVAL_GLOBAL) != TCL_OK) {
        Tcl_DecrRefCount(cmd);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(cmd);

    Tcl_Obj *children = Tcl_GetObjResult(interp);

    if (Tcl_ListObjGetElements(interp, children, &count, &elems) == TCL_OK) {
        for (int i = 0; i < count; i++) {
            const char *child = Tcl_GetString(elems[i]);

            if (strncmp(child, "::tcl", 5) == 0 ||
                strncmp(child, "::oo", 4) == 0)
                continue;

            if (AppendNamespaceVarsRec(interp, script, child) != TCL_OK)
                return TCL_ERROR;
        }
    }

    return TCL_OK;
}

/* =========================
 * Append namespaces
 * ========================= */
static int AppendNamespaces(Tcl_Interp *interp, Tcl_Obj *script) {
    if (Tcl_Eval(interp, "namespace children ::") != TCL_OK)
        return TCL_ERROR;

    Tcl_Obj *list = Tcl_GetObjResult(interp);

    int count;
    Tcl_Obj **elems;

    if (Tcl_ListObjGetElements(interp, list, &count, &elems) != TCL_OK)
        return TCL_ERROR;

    for (int i = 0; i < count; i++) {
        const char *ns = Tcl_GetString(elems[i]);

        if (strncmp(ns, "::tcl", 5) == 0 ||
            strncmp(ns, "::oo", 4) == 0)
            continue;

        Tcl_AppendPrintfToObj(script,
            "namespace eval %s {}\n", ns);
    }

    return TCL_OK;
}

/* =========================
 * Append procs
 * ========================= */
static int AppendProcs(Tcl_Interp *interp, Tcl_Obj *script) {
    if (Tcl_Eval(interp, "info procs") != TCL_OK)
        return TCL_ERROR;

    Tcl_Obj *list = Tcl_GetObjResult(interp);

    int count;
    Tcl_Obj **elems;

    if (Tcl_ListObjGetElements(interp, list, &count, &elems) != TCL_OK)
        return TCL_ERROR;

    for (int i = 0; i < count; i++) {
        const char *name = Tcl_GetString(elems[i]);

        if (strncmp(name, "::tcl", 5) == 0)
            continue;

        Tcl_Obj *argsCmd = Tcl_ObjPrintf("info args %s", name);
        if (Tcl_EvalObjEx(interp, argsCmd, TCL_EVAL_GLOBAL) != TCL_OK) {
            Tcl_DecrRefCount(argsCmd);
            continue;
        }
        Tcl_DecrRefCount(argsCmd);

        Tcl_Obj *args = Tcl_GetObjResult(interp);
        Tcl_IncrRefCount(args);

        Tcl_Obj *bodyCmd = Tcl_ObjPrintf("info body %s", name);
        if (Tcl_EvalObjEx(interp, bodyCmd, TCL_EVAL_GLOBAL) != TCL_OK) {
            Tcl_DecrRefCount(bodyCmd);
            Tcl_DecrRefCount(args);
            continue;
        }
        Tcl_DecrRefCount(bodyCmd);

        Tcl_Obj *body = Tcl_GetObjResult(interp);
        Tcl_IncrRefCount(body);

        Tcl_Obj *line = Tcl_ObjPrintf("proc %s ", name);
        Tcl_AppendObjToObj(line, args);
        Tcl_AppendToObj(line, " ", -1);
        Tcl_AppendObjToObj(line, body);
        Tcl_AppendToObj(line, "\n", -1);

        Tcl_AppendObjToObj(script, line);

        Tcl_DecrRefCount(args);
        Tcl_DecrRefCount(body);
    }

    return TCL_OK;
}

/* =========================
 * PUBLIC: Build snapshot
 * ========================= */
Tcl_Obj *BuildInterpSnapshot(Tcl_Interp *interp) {
    Tcl_Obj *script = Tcl_NewObj();
    Tcl_IncrRefCount(script);

    Tcl_AppendToObj(script, "# --- SNAPSHOT START ---\n", -1);

    if (AppendNamespaces(interp, script) != TCL_OK) goto error;
    if (AppendProcs(interp, script) != TCL_OK) goto error;
    if (AppendGlobals(interp, script) != TCL_OK) goto error;
    if (AppendNamespaceVarsRec(interp, script, "::") != TCL_OK) goto error;

    Tcl_AppendToObj(script, "# --- SNAPSHOT END ---\n", -1);

    return script;

error:
    Tcl_DecrRefCount(script);
    return NULL;
}

/* =========================
 * PUBLIC: Replay snapshot
 * ========================= */
int ReplayInterpSnapshot(Tcl_Interp *interp, Tcl_Obj *snapshot) {
    return Tcl_EvalObjEx(interp, snapshot, TCL_EVAL_GLOBAL);
}
