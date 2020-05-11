#include "config.h"
#include "mono/utils/mono-compiler.h"

#ifdef ENABLE_NETCORE // MonoAssemblyLoadContext support only in netcore Mono

#include "mono/metadata/assembly.h"
#include "mono/metadata/domain-internals.h"
#include "mono/metadata/exception-internals.h"
#include "mono/metadata/icall-decl.h"
#include "mono/metadata/loader-internals.h"
#include "mono/metadata/loaded-images-internals.h"
#include "mono/metadata/mono-private-unstable.h"
#include "mono/utils/mono-error-internals.h"
#include "mono/utils/mono-logger-internals.h"

GENERATE_GET_CLASS_WITH_CACHE (assembly_load_context, "System.Runtime.Loader", "AssemblyLoadContext");
static GENERATE_GET_CLASS_WITH_CACHE (loader_allocator, "System.Reflection", "LoaderAllocator");
static GENERATE_GET_CLASS_WITH_CACHE (loader_allocator_scout, "System.Reflection", "LoaderAllocatorScout");

static void
mono_alc_free (MonoAssemblyLoadContext *alc);

static inline void
mono_domain_alcs_lock (MonoDomain *domain)
{
	mono_coop_mutex_lock (&domain->alcs_lock);
}

static inline void
mono_domain_alcs_unlock (MonoDomain *domain)
{
	mono_coop_mutex_unlock (&domain->alcs_lock);
}

static MonoAssemblyLoadContext *
mono_domain_create_alc (MonoDomain *domain, gboolean is_default, gboolean collectible)
{
	MonoAssemblyLoadContext *alc = NULL;

	mono_domain_alcs_lock (domain);
	if (is_default && domain->default_alc)
		goto leave;

	alc = g_new0 (MonoAssemblyLoadContext, 1);
	mono_alc_init (alc, domain, collectible);

	domain->alcs = g_slist_prepend (domain->alcs, alc);
	if (is_default)
		domain->default_alc = alc;
	if (collectible) {
		mono_loader_allocator_addref (alc->loader_allocator);
		domain->collectible_loader_allocators = g_slist_prepend (domain->collectible_loader_allocators, alc->loader_allocator);
	}

leave:
	mono_domain_alcs_unlock (domain);
	return alc;
}

static void
mono_loader_allocator_init (MonoLoaderAllocator *loader_allocator, MonoAssemblyLoadContext *alc, gboolean collectible)
{
	MonoLoadedImages *li = g_new0 (MonoLoadedImages, 1);
	mono_loaded_images_init (li, alc);

	loader_allocator->alc = alc;
	loader_allocator->collectible = collectible;
	loader_allocator->ref_count = 1; // the ALC
	loader_allocator->loaded_images = li;
	loader_allocator->loaded_assemblies = NULL;
	mono_coop_mutex_init (&loader_allocator->assemblies_lock);
}

void
mono_alc_init (MonoAssemblyLoadContext *alc, MonoDomain *domain, gboolean collectible)
{
	MonoLoaderAllocator *loader_allocator = g_new0 (MonoLoaderAllocator, 1);
	mono_loader_allocator_init (loader_allocator, alc, collectible);

	alc->domain = domain;
	alc->loader_allocator = loader_allocator;
	alc->collectible = collectible;
	alc->unloading = FALSE;
	mono_coop_mutex_init (&alc->pinvoke_lock);
	alc->pinvoke_scopes = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);	
}

void
mono_domain_create_default_alc (MonoDomain *domain)
{
	if (domain->default_alc)
		return;
	mono_domain_create_alc (domain, TRUE, FALSE);
}

MonoAssemblyLoadContext *
mono_domain_create_individual_alc (MonoDomain *domain, MonoGCHandle this_gchandle, gboolean collectible, MonoError *error)
{
	MonoAssemblyLoadContext *alc = mono_domain_create_alc (domain, FALSE, collectible);

	if (collectible) {
		// Create managed LoaderAllocator and LoaderAllocatorScout
		// TODO: handle failure case
		MonoClass *la_class = mono_class_get_loader_allocator_class ();
		MonoManagedLoaderAllocatorHandle managed_la = MONO_HANDLE_CAST (MonoManagedLoaderAllocator, mono_object_new_handle (domain, la_class, error));

		MonoClass *la_scout_class = mono_class_get_loader_allocator_scout_class ();
		MonoManagedLoaderAllocatorScoutHandle managed_la_scout = MONO_HANDLE_CAST (MonoManagedLoaderAllocatorScout, mono_object_new_handle (domain, la_scout_class, error));

		MONO_HANDLE_SET (managed_la, scout, managed_la_scout);
		MONO_HANDLE_SETVAL (managed_la_scout, native_loader_allocator, MonoLoaderAllocator *, alc->loader_allocator);

		alc->loader_allocator->gchandle = mono_gchandle_new_weakref_from_handle_track_resurrection (MONO_HANDLE_CAST (MonoObject, managed_la));
		alc->loader_allocator_gchandle = mono_gchandle_from_handle (MONO_HANDLE_CAST (MonoObject, managed_la), FALSE);
	}

	alc->gchandle = this_gchandle;
	return alc;
}

// LOCKING: assumes the domain alcs_lock is taken
static void
mono_loader_allocator_cleanup (MonoLoaderAllocator *loader_allocator)
{
	/*
	 * The minimum refcount on assemblies is 2: one for the domain and one for the ALC. 
	 * The domain refcount might be less than optimal on netcore, but its removal is too likely to cause issues for now.
	 */
	GSList *tmp;
	MonoAssemblyLoadContext *alc = loader_allocator->alc;
	MonoDomain *domain = alc->domain;

	g_assert (alc != mono_domain_default_alc (domain));
	g_assert (alc->collectible == TRUE);

	// Remove the assemblies from domain_assemblies
	mono_domain_assemblies_lock (domain);
	for (tmp = loader_allocator->loaded_assemblies; tmp; tmp = tmp->next) {
		MonoAssembly *assembly = (MonoAssembly *)tmp->data;
		domain->domain_assemblies = g_slist_remove (domain->domain_assemblies, assembly);
		mono_assembly_decref (assembly);
		mono_trace (G_LOG_LEVEL_DEBUG, MONO_TRACE_ASSEMBLY, "Unloading ALC [%p], removing assembly %s[%p] from domain_assemblies, ref_count=%d\n", alc, assembly->aname.name, assembly, assembly->ref_count);
	}
	mono_domain_assemblies_unlock (domain);

	// Scan here to assert no lingering references in vtables? Seems useful but idk enough about the GC to make this happen

	// Release the GC roots
	for (tmp = loader_allocator->loaded_assemblies; tmp; tmp = tmp->next) {
		MonoAssembly *assembly = (MonoAssembly *)tmp->data;
		mono_assembly_release_gc_roots (assembly);
	}

	// Close dynamic assemblies
	for (tmp = loader_allocator->loaded_assemblies; tmp; tmp = tmp->next) {
		MonoAssembly *assembly = (MonoAssembly *)tmp->data;
		if (!assembly->image || !image_is_dynamic (assembly->image))
			continue;
		mono_trace (G_LOG_LEVEL_DEBUG, MONO_TRACE_ASSEMBLY, "Unloading ALC [%p], dynamic assembly %s[%p], ref_count=%d", domain, assembly->aname.name, assembly, assembly->ref_count);
		if (!mono_assembly_close_except_image_pools (assembly))
			tmp->data = NULL;
	}

	// Close the remaining assemblies
	for (tmp = loader_allocator->loaded_assemblies; tmp; tmp = tmp->next) {
		MonoAssembly *assembly = (MonoAssembly *)tmp->data;
		if (!assembly)
			continue;
		if (!assembly->image || image_is_dynamic (assembly->image))
			continue;
		mono_trace (G_LOG_LEVEL_DEBUG, MONO_TRACE_ASSEMBLY, "Unloading ALC [%p], non-dynamic assembly %s[%p], ref_count=%d", domain, assembly->aname.name, assembly, assembly->ref_count);
		if (!mono_assembly_close_except_image_pools (assembly))
			tmp->data = NULL;
	}

	// Complete the second closing pass on lingering assemblies
	for (tmp = loader_allocator->loaded_assemblies; tmp; tmp = tmp->next) {
		MonoAssembly *assembly = (MonoAssembly *)tmp->data;
		if (assembly)
			mono_assembly_close_finish (assembly);
	}

	// Free the loaded_assemblies
	g_slist_free (loader_allocator->loaded_assemblies);
	loader_allocator->loaded_assemblies = NULL;

	mono_gchandle_free_internal (loader_allocator->gchandle);
	loader_allocator->gchandle = NULL;

	mono_coop_mutex_destroy (&loader_allocator->assemblies_lock);

	mono_loaded_images_free (loader_allocator->loaded_images);
	loader_allocator->loaded_images = NULL;

	// TODO: free mempool stuff/jit info tables, see domain freeing for an example

	mono_alc_free (loader_allocator->alc);
	loader_allocator->alc = NULL;
}

static void
mono_loader_allocator_free (MonoLoaderAllocator *loader_allocator)
{
	mono_loader_allocator_cleanup (loader_allocator);
	g_free (loader_allocator);
}

// LOCKING: assumes the domain alcs_lock is taken
void
mono_alc_cleanup (MonoAssemblyLoadContext *alc)
{
	MonoDomain *domain = alc->domain;

	g_assert (alc != mono_domain_default_alc (domain));
	g_assert (alc->collectible == TRUE);

	mono_gchandle_free_internal (alc->gchandle);

	g_hash_table_destroy (alc->pinvoke_scopes);
	mono_coop_mutex_destroy (&alc->pinvoke_lock);

	domain->alcs = g_slist_remove (domain->alcs, alc);

	// TODO: alc unloaded profiler event
}

gint32
mono_loader_allocator_addref (MonoLoaderAllocator *loader_allocator)
{
	g_assert (loader_allocator->ref_count > 0);
	return mono_atomic_inc_i32 (&loader_allocator->ref_count);
}

gint32
mono_loader_allocator_decref (MonoLoaderAllocator *loader_allocator)
{
	g_assert (loader_allocator->ref_count > 0);
	return mono_atomic_dec_i32 (&loader_allocator->ref_count);
}

void
mono_alc_assemblies_lock (MonoAssemblyLoadContext *alc)
{
	mono_coop_mutex_lock (&alc->loader_allocator->assemblies_lock);
}

void
mono_alc_assemblies_unlock (MonoAssemblyLoadContext *alc)
{
	mono_coop_mutex_unlock (&alc->loader_allocator->assemblies_lock);
}

static void
mono_alc_free (MonoAssemblyLoadContext *alc)
{
	mono_alc_cleanup (alc);
	g_free (alc);
}

static void
mono_domain_collect_loader_allocators (MonoDomain *domain)
{
	GSList *tmp;

	mono_domain_alcs_lock (domain);

	for (tmp = domain->collectible_loader_allocators; tmp; tmp = tmp->next) {
		MonoLoaderAllocator *loader_allocator = (MonoLoaderAllocator *)tmp->data;
		if (loader_allocator->ref_count == 0) {
			mono_loader_allocator_free (loader_allocator);
			tmp->data = NULL;
		}
	}

	domain->collectible_loader_allocators = g_slist_remove_all (domain->collectible_loader_allocators, NULL);

	mono_domain_alcs_unlock (domain);
}

gpointer
ves_icall_System_Runtime_Loader_AssemblyLoadContext_InternalInitializeNativeALC (gpointer this_gchandle_ptr, MonoBoolean is_default_alc, MonoBoolean collectible, MonoError *error)
{
	/* If the ALC is collectible, this_gchandle is weak, otherwise it's strong. */
	MonoGCHandle this_gchandle = (MonoGCHandle)this_gchandle_ptr;

	MonoDomain *domain = mono_domain_get ();
	MonoAssemblyLoadContext *alc = NULL;

	if (is_default_alc) {
		alc = mono_domain_default_alc (domain);
		g_assert (alc);
		if (!alc->gchandle)
			alc->gchandle = this_gchandle;
	} else {
		/* create it */
		alc = mono_domain_create_individual_alc (domain, this_gchandle, collectible, error);
	}
	return alc;
}

void
ves_icall_System_Runtime_Loader_AssemblyLoadContext_PrepareForAssemblyLoadContextRelease (gpointer alc_pointer, gpointer strong_gchandle_ptr, MonoError *error)
{
	MonoGCHandle strong_gchandle = (MonoGCHandle)strong_gchandle_ptr;
	MonoAssemblyLoadContext *alc = (MonoAssemblyLoadContext *)alc_pointer;

	g_assert (alc->collectible == TRUE);
	g_assert (alc->unloading == FALSE);
	g_assert (alc->gchandle);
	g_assert (alc->loader_allocator);
	g_assert (alc->loader_allocator_gchandle);

	alc->unloading = TRUE;

	// Replace the weak gchandle with the new strong one to keep the managed ALC alive
	MonoGCHandle weak_gchandle = alc->gchandle;
	alc->gchandle = strong_gchandle;
	mono_gchandle_free_internal (weak_gchandle);

	mono_loader_allocator_decref (alc->loader_allocator);
	//alc->loader_allocator = NULL; // We can't actually do this RN until we replace some cases of ALCs with LoaderAllocators (see the MonoLoadedImages crash), but unsure if beneficial to do so

	// Destroy the strong handle to the managed LoaderAllocator to let it reach its finalizer
	mono_gchandle_free_internal (alc->loader_allocator_gchandle);
	alc->loader_allocator_gchandle = NULL;
}

MonoBoolean
ves_icall_System_Reflection_LoaderAllocatorScout_Destroy (gpointer la_pointer, MonoError *error)
{
	MonoLoaderAllocator *loader_allocator = (MonoLoaderAllocator *)la_pointer;
	MonoDomain *domain = loader_allocator->alc->domain;

	// Check if the managed LoaderAllocator is still hanging around
	if (!MONO_HANDLE_IS_NULL (mono_gchandle_get_target_handle (loader_allocator->gchandle)))
		return FALSE;

	// decrement refcount of all LAs referenced by this LA?
	// decrement refcount of this LA?
	gint32 ref_count = mono_loader_allocator_decref (loader_allocator);

	if (ref_count == 0)
		mono_domain_collect_loader_allocators (domain);

	return TRUE;
}

gpointer
ves_icall_System_Runtime_Loader_AssemblyLoadContext_GetLoadContextForAssembly (MonoReflectionAssemblyHandle assm_obj, MonoError *error)
{
	MonoAssembly *assm = MONO_HANDLE_GETVAL (assm_obj, assembly);
	MonoAssemblyLoadContext *alc = mono_assembly_get_alc (assm);

	return (gpointer)alc->gchandle;
}

gboolean
mono_alc_is_default (MonoAssemblyLoadContext *alc)
{
	return alc == mono_alc_domain (alc)->default_alc;
}

MonoAssemblyLoadContext *
mono_alc_from_gchandle (MonoGCHandle alc_gchandle)
{
	MonoManagedAssemblyLoadContextHandle managed_alc = MONO_HANDLE_CAST (MonoManagedAssemblyLoadContext, mono_gchandle_get_target_handle (alc_gchandle));
	MonoAssemblyLoadContext *alc = (MonoAssemblyLoadContext *)MONO_HANDLE_GETVAL (managed_alc, native_assembly_load_context);
	return alc;
}

MonoGCHandle
mono_alc_get_default_gchandle (void)
{
	// Because the default domain is never unloadable, this should be a strong handle and never change
	return mono_domain_default_alc (mono_domain_get ())->gchandle;
}

static MonoAssembly*
invoke_resolve_method (MonoMethod *resolve_method, MonoAssemblyLoadContext *alc, MonoAssemblyName *aname, MonoError *error)
{
	MonoAssembly *result = NULL;
	char* aname_str = NULL;

	if (mono_runtime_get_no_exec ())
		return NULL;

	HANDLE_FUNCTION_ENTER ();

	aname_str = mono_stringify_assembly_name (aname);

	MonoStringHandle aname_obj = mono_string_new_handle (mono_alc_domain (alc), aname_str, error);
	goto_if_nok (error, leave);

	MonoReflectionAssemblyHandle assm;
	gpointer gchandle;
	gchandle = (gpointer)alc->gchandle;
	gpointer args [2];
	args [0] = &gchandle;
	args [1] = MONO_HANDLE_RAW (aname_obj);
	assm = MONO_HANDLE_CAST (MonoReflectionAssembly, mono_runtime_try_invoke_handle (resolve_method, NULL_HANDLE, args, error));
	goto_if_nok (error, leave);

	if (MONO_HANDLE_BOOL (assm))
		result = MONO_HANDLE_GETVAL (assm, assembly);

leave:
	g_free (aname_str);
	HANDLE_FUNCTION_RETURN_VAL (result);
}

static MonoAssembly*
mono_alc_invoke_resolve_using_load (MonoAssemblyLoadContext *alc, MonoAssemblyName *aname, MonoError *error)
{
	MONO_STATIC_POINTER_INIT (MonoMethod, resolve)

		ERROR_DECL (local_error);
		MonoClass *alc_class = mono_class_get_assembly_load_context_class ();
		g_assert (alc_class);
		resolve = mono_class_get_method_from_name_checked (alc_class, "MonoResolveUsingLoad", -1, 0, local_error);
		mono_error_assert_ok (local_error);

	MONO_STATIC_POINTER_INIT_END (MonoMethod, resolve)

	g_assert (resolve);

	return invoke_resolve_method (resolve, alc, aname, error);
}

MonoAssembly*
mono_alc_invoke_resolve_using_load_nofail (MonoAssemblyLoadContext *alc, MonoAssemblyName *aname)
{
	MonoAssembly *result = NULL;
	ERROR_DECL (error);

	result = mono_alc_invoke_resolve_using_load (alc, aname, error);
	if (!is_ok (error))
		mono_trace (G_LOG_LEVEL_DEBUG, MONO_TRACE_ASSEMBLY, "Error while invoking ALC Load(\"%s\") method: '%s'", aname->name, mono_error_get_message (error));

	mono_error_cleanup (error);

	return result;
}

static MonoAssembly*
mono_alc_invoke_resolve_using_resolving_event (MonoAssemblyLoadContext *alc, MonoAssemblyName *aname, MonoError *error)
{
	MONO_STATIC_POINTER_INIT (MonoMethod, resolve)

		ERROR_DECL (local_error);
		MonoClass *alc_class = mono_class_get_assembly_load_context_class ();
		g_assert (alc_class);
		resolve = mono_class_get_method_from_name_checked (alc_class, "MonoResolveUsingResolvingEvent", -1, 0, local_error);
		mono_error_assert_ok (local_error);

	MONO_STATIC_POINTER_INIT_END (MonoMethod, resolve)

	g_assert (resolve);

	return invoke_resolve_method (resolve, alc, aname, error);
}

MonoAssembly*
mono_alc_invoke_resolve_using_resolving_event_nofail (MonoAssemblyLoadContext *alc, MonoAssemblyName *aname)
{
	MonoAssembly *result = NULL;
	ERROR_DECL (error);

	result = mono_alc_invoke_resolve_using_resolving_event (alc, aname, error);
	if (!is_ok (error))
		mono_trace (G_LOG_LEVEL_DEBUG, MONO_TRACE_ASSEMBLY, "Error while invoking ALC Resolving(\"%s\") event: '%s'", aname->name, mono_error_get_message (error));

	mono_error_cleanup (error);

	return result;
}

static MonoAssembly*
mono_alc_invoke_resolve_using_resolve_satellite (MonoAssemblyLoadContext *alc, MonoAssemblyName *aname, MonoError *error)
{
	MONO_STATIC_POINTER_INIT (MonoMethod, resolve)

		ERROR_DECL (local_error);
		MonoClass *alc_class = mono_class_get_assembly_load_context_class ();
		g_assert (alc_class);
		resolve = mono_class_get_method_from_name_checked (alc_class, "MonoResolveUsingResolveSatelliteAssembly", -1, 0, local_error);
		mono_error_assert_ok (local_error);

	MONO_STATIC_POINTER_INIT_END (MonoMethod, resolve)

	g_assert (resolve);

	return invoke_resolve_method (resolve, alc, aname, error);
}

MonoAssembly*
mono_alc_invoke_resolve_using_resolve_satellite_nofail (MonoAssemblyLoadContext *alc, MonoAssemblyName *aname)
{
	MonoAssembly *result = NULL;
	ERROR_DECL (error);

	result = mono_alc_invoke_resolve_using_resolve_satellite (alc, aname, error);
	if (!is_ok (error))
		mono_trace (G_LOG_LEVEL_DEBUG, MONO_TRACE_ASSEMBLY, "Error while invoking ALC ResolveSatelliteAssembly(\"%s\") method: '%s'", aname->name, mono_error_get_message (error));

	mono_error_cleanup (error);

	return result;
}

#endif /* ENABLE_NETCORE */

MONO_EMPTY_SOURCE_FILE (assembly_load_context)
