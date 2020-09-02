#include <mono/metadata/loader-internals.h>
#include <mono/metadata/gc-internals.h>
#include <mono/metadata/reflection-cache.h>
#include <mono/metadata/mono-hash-internals.h>

static void
memory_manager_init (MonoMemoryManager *memory_manager, gboolean collectible)
{
	MonoDomain *domain = mono_domain_get (); // this is quite possibly wrong on legacy?

	memory_manager->freeing = FALSE;

	mono_coop_mutex_init_recursive (&memory_manager->lock);

	memory_manager->mp = mono_mempool_new ();
	memory_manager->code_mp = mono_code_manager_new ();

	memory_manager->class_vtable_array = g_ptr_array_new ();

	// TODO: make these not linked to the domain for debugging
	memory_manager->type_hash = mono_g_hash_table_new_type_internal ((GHashFunc)mono_metadata_type_hash, (GCompareFunc)mono_metadata_type_equal, MONO_HASH_VALUE_GC, MONO_ROOT_SOURCE_DOMAIN, domain, "Domain Reflection Type Table");
	memory_manager->refobject_hash = mono_conc_g_hash_table_new_type (mono_reflected_hash, mono_reflected_equal, MONO_HASH_VALUE_GC, MONO_ROOT_SOURCE_DOMAIN, domain, "Domain Reflection Object Table");
	memory_manager->type_init_exception_hash = mono_g_hash_table_new_type_internal (mono_aligned_addr_hash, NULL, MONO_HASH_VALUE_GC, MONO_ROOT_SOURCE_DOMAIN, domain, "Domain Type Initialization Exception Table");
}

MonoSingletonMemoryManager *
mono_memory_manager_create_singleton (MonoAssemblyLoadContext *alc, gboolean collectible)
{
	MonoSingletonMemoryManager *mem_manager = g_new0 (MonoSingletonMemoryManager, 1);
	memory_manager_init ((MonoMemoryManager *)mem_manager, collectible);

	mem_manager->memory_manager.is_generic = FALSE;
	mem_manager->alc = alc;

	return mem_manager;
}

static void
cleanup_refobject_hash (gpointer key, gpointer value, gpointer user_data)
{
	free_reflected_entry ((ReflectedEntry *)key);
}

static void
unregister_vtable_reflection_type (MonoVTable *vtable)
{
	MonoObject *type = (MonoObject *)vtable->type;

	if (type->vtable->klass != mono_defaults.runtimetype_class)
		MONO_GC_UNREGISTER_ROOT_IF_MOVING (vtable->type);
}

static void
memory_manager_delete (MonoMemoryManager *memory_manager, gboolean debug_unload)
{
	// Scan here to assert no lingering references in vtables?

	mono_coop_mutex_destroy (&memory_manager->lock);

	if (debug_unload) {
		mono_mempool_invalidate (memory_manager->mp);
		mono_code_manager_invalidate (memory_manager->code_mp);
	} else {
#ifndef DISABLE_PERFCOUNTERS
		/* FIXME: use an explicit subtraction method as soon as it's available */
		mono_atomic_fetch_add_i32 (&mono_perfcounters->loader_bytes, -1 * mono_mempool_get_allocated (memory_manager->mp));
#endif
		mono_mempool_destroy (memory_manager->mp);
		memory_manager->mp = NULL;
		mono_code_manager_destroy (memory_manager->code_mp);
		memory_manager->code_mp = NULL;
	}

	g_ptr_array_free (memory_manager->class_vtable_array, TRUE);
	memory_manager->class_vtable_array = NULL;

	// Must be done before type_hash is freed
	for (int i = 0; i < memory_manager->class_vtable_array->len; i++)
		unregister_vtable_reflection_type ((MonoVTable *)g_ptr_array_index (memory_manager->class_vtable_array, i));

	mono_g_hash_table_destroy (memory_manager->type_hash);
	memory_manager->type_hash = NULL;
	mono_conc_g_hash_table_foreach (memory_manager->refobject_hash, cleanup_refobject_hash, NULL);
	mono_conc_g_hash_table_destroy (memory_manager->refobject_hash);
	memory_manager->refobject_hash = NULL;
	mono_g_hash_table_destroy (memory_manager->type_init_exception_hash);
	memory_manager->type_init_exception_hash = NULL;
}

void
mono_memory_manager_free_singleton (MonoSingletonMemoryManager *memory_manager, gboolean debug_unload)
{
	g_assert (!memory_manager->memory_manager.is_generic);

	memory_manager_delete ((MonoMemoryManager *)memory_manager, debug_unload);
	g_free (memory_manager);
}
