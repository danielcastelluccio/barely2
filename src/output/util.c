#include <assert.h>

#include "util.h"

Type evaluate_output_type(Type* type, Generic_State* state) {
    switch (type->kind) {
        case Type_Basic: {
            Basic_Type* basic = &type->data.basic;
            Item_Node* item = basic->resolved_node;

            if (item == NULL) {
                Identifier identifier = basic->identifier;
                Resolved resolved = resolve(state, identifier);
                if (resolved.kind == Resolved_Item) {
                    item = resolved.data.item;
                }
            }

            if (item != NULL) {
                assert(item->kind == Item_Type);
                Type_Node* type_node = &item->data.type;
                Type type_result = type_node->type;
                return type_result;
            }
            break;
        }
        case Type_TypeOf: {
            return *type->data.type_of.computed_result_type;
        }
        case Type_RunMacro: {
            assert(type->data.run_macro.computed_type != NULL);
            return *type->data.run_macro.computed_type;
        }
        default:
            return *type;
    }
    return *type;
}
