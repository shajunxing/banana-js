/*
Copyright 2024 ShaJunXing <shajunxing@hotmail.com>

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "js.h"

static enum js_token_state _get_token_state(struct js *pjs) {
    // check end here
    if (pjs->tok_cache_idx < pjs->tok_cache_len) {
        return pjs->tok_cache[pjs->tok_cache_idx].stat;
    } else {
        return ts_end_of_file;
    }
}

static char *_get_token_head(struct js *pjs) {
    return pjs->src + pjs->tok_cache[pjs->tok_cache_idx].h;
}

static size_t _get_token_length(struct js *pjs) {
    return pjs->tok_cache[pjs->tok_cache_idx].t - pjs->tok_cache[pjs->tok_cache_idx].h;
}

static long _get_token_line(struct js *pjs) {
    return pjs->tok_cache[pjs->tok_cache_idx].line;
}

static char *_get_token_string_head(struct js *pjs) {
    return _get_token_head(pjs) + 1;
}

static size_t _get_token_string_length(struct js *pjs) {
    return _get_token_length(pjs) - 2;
}

static double _get_token_number(struct js *pjs) {
    return pjs->tok_cache[pjs->tok_cache_idx].num;
}

static void _next_token(struct js *pjs) {
    // DONT check end here, because _accept() will step next on success, if end here, many operations will not done.
    pjs->tok_cache_idx++;
}

static void _stack_forward(struct js *pjs) {
    js_stack_forward(pjs, pjs->tok_cache[pjs->tok_cache_idx].h, pjs->tok_cache[pjs->tok_cache_idx].t);
}

static bool _accept(struct js *pjs, enum js_token_state stat) {
    if (_get_token_state(pjs) == stat) {
        _next_token(pjs);
        return true;
    }
    return false;
}

// use macro instead of  function, to correctly record error position
// DONT use '_pjs' inside do while, or msvc will cause lots of 'warning C4700: uninitialized local variable '_pjs' used', don't know why, maybe bug
#define _expect(pjs, stat, msg)        \
    do {                               \
        struct js *__pjs = (pjs);      \
        if (!_accept(__pjs, (stat))) { \
            js_throw(__pjs, (msg));    \
        }                              \
    } while (0)

static bool _require_exec(struct js *pjs) {
    bool exec = pjs->parse_exec && !pjs->mark_break && !pjs->mark_continue && !pjs->mark_return;
    log("%s", exec ? "Parse and execute" : "Parse only");
    return exec;
}

static void _parse_statement(struct js *pjs);

static void _parse_function(struct js *pjs) {
    if (_require_exec(pjs)) {
        char *ident_h;
        size_t ident_len;
        size_t i = 0, j;
        struct js_value param;
        _expect(pjs, ts_left_parenthesis, "Expect (");
        if (!_accept(pjs, ts_right_parenthesis)) {
            for (;; i++) {
                if (_accept(pjs, ts_spread)) {
                    // rest parameters
                    if (_get_token_state(pjs) != ts_identifier) {
                        js_throw(pjs, "Expect variable name");
                    }
                    ident_h = _get_token_head(pjs);
                    ident_len = _get_token_length(pjs);
                    _next_token(pjs);
                    param = js_array(pjs);
                    for (j = i; j < js_parameter_length(pjs); j++) {
                        js_array_push(pjs, param, js_parameter_get(pjs, j));
                    }
                    js_variable_declare(pjs, ident_h, ident_len, param);
                    _expect(pjs, ts_right_parenthesis, "Expect )");
                    break;
                } else {
                    if (_get_token_state(pjs) != ts_identifier) {
                        js_throw(pjs, "Expect variable name");
                    }
                    ident_h = _get_token_head(pjs);
                    ident_len = _get_token_length(pjs);
                    _next_token(pjs);
                    if (_accept(pjs, ts_assignment)) {
                        js_variable_declare(pjs, ident_h, ident_len, js_parse_expression(pjs));
                    } else {
                        js_variable_declare(pjs, ident_h, ident_len, js_null());
                    }
                    param = js_parameter_get(pjs, i);
                    if (param.type != vt_null) {
                        js_variable_assign(pjs, ident_h, ident_len, param);
                    }
                    if (_accept(pjs, ts_comma)) {
                        continue;
                    } else if (_accept(pjs, ts_right_parenthesis)) {
                        break;
                    } else {
                        js_throw(pjs, "Expect , or )");
                    }
                }
            }
        }
        _expect(pjs, ts_left_brace, "Expect {");
        while (_get_token_state(pjs) != ts_right_brace) {
            _parse_statement(pjs);
        }
        _next_token(pjs);
    } else {
        _expect(pjs, ts_left_parenthesis, "Expect (");
        if (!_accept(pjs, ts_right_parenthesis)) {
            for (;;) {
                if (_accept(pjs, ts_spread)) {
                    _expect(pjs, ts_identifier, "Expect variable name");
                    _expect(pjs, ts_right_parenthesis, "Expect )");
                    break;
                } else {
                    _expect(pjs, ts_identifier, "Expect variable name");
                    if (_accept(pjs, ts_assignment)) {
                        js_parse_expression(pjs);
                    }
                    if (_accept(pjs, ts_comma)) {
                        continue;
                    } else if (_accept(pjs, ts_right_parenthesis)) {
                        break;
                    } else {
                        js_throw(pjs, "Expect , or )");
                    }
                }
            }
        }
        _expect(pjs, ts_left_brace, "Expect {");
        while (_get_token_state(pjs) != ts_right_brace) {
            _parse_statement(pjs);
        }
        _next_token(pjs);
    }
}

static struct js_value _parse_value(struct js *pjs) {
    if (_require_exec(pjs)) {
        struct js_value ret;
        struct js_value spread;
        size_t i;
        char *k;
        size_t klen;
        switch (_get_token_state(pjs)) {
        case ts_null:
            ret = js_null();
            _next_token(pjs);
            break;
        case ts_true:
            ret = js_boolean(true);
            _next_token(pjs);
            break;
        case ts_false:
            ret = js_boolean(false);
            _next_token(pjs);
            break;
        case ts_number:
            ret = js_number(_get_token_number(pjs));
            _next_token(pjs);
            break;
        case ts_string:
            ret = js_string(pjs, _get_token_string_head(pjs), _get_token_string_length(pjs));
            _next_token(pjs);
            break;
        case ts_left_bracket:
            _next_token(pjs);
            ret = js_array(pjs);
            if (!_accept(pjs, ts_right_bracket)) {
                for (;;) {
                    if (_accept(pjs, ts_spread)) {
                        spread = js_parse_expression(pjs);
                        if (spread.type != vt_array) {
                            js_throw(pjs, "Operator ... requires array operand");
                        }
                        for (i = 0; i < spread.value.array->len; i++) {
                            js_array_push(pjs, ret, js_array_get(pjs, spread, i));
                        }
                    } else {
                        js_array_push(pjs, ret, js_parse_expression(pjs));
                    }
                    if (_accept(pjs, ts_comma)) {
                        continue;
                    } else if (_accept(pjs, ts_right_bracket)) {
                        break;
                    } else {
                        js_throw(pjs, "Expect , or ]");
                    }
                }
            }
            break;
        case ts_left_brace:
            _next_token(pjs);
            ret = js_object(pjs);
            if (!_accept(pjs, ts_right_brace)) {
                for (;;) {
                    if (_get_token_state(pjs) == ts_string) {
                        k = _get_token_string_head(pjs);
                        klen = _get_token_string_length(pjs);
                    } else if (_get_token_state(pjs) == ts_identifier) {
                        k = _get_token_head(pjs);
                        klen = _get_token_length(pjs);
                    } else {
                        js_throw(pjs, "Expect string or identifier");
                    }
                    _next_token(pjs);
                    _expect(pjs, ts_colon, "Expect :");
                    js_object_put(pjs, ret, k, klen, js_parse_expression(pjs));
                    if (_accept(pjs, ts_comma)) {
                        continue;
                    } else if (_accept(pjs, ts_right_brace)) {
                        break;
                    } else {
                        js_throw(pjs, "Expect , or }");
                    }
                }
            }
            break;
        case ts_function:
            _next_token(pjs);
            ret = js_function(pjs, pjs->tok_cache_idx);
            // only walk through, no exec
            pjs->parse_exec = false;
            _parse_function(pjs);
            pjs->parse_exec = true;
            break;
        default:
            js_throw(pjs, "Not a value literal");
        }
        return ret;
    } else {
        switch (_get_token_state(pjs)) {
        case ts_null:
        case ts_true:
        case ts_false:
        case ts_number:
        case ts_string:
            _next_token(pjs);
            break;
        case ts_left_bracket:
            _next_token(pjs);
            if (!_accept(pjs, ts_right_bracket)) {
                for (;;) {
                    _accept(pjs, ts_spread); // accept or not
                    js_parse_expression(pjs);
                    if (_accept(pjs, ts_comma)) {
                        continue;
                    } else if (_accept(pjs, ts_right_bracket)) {
                        break;
                    } else {
                        js_throw(pjs, "Expect , or ]");
                    }
                }
            }
            break;
        case ts_left_brace:
            _next_token(pjs);
            if (!_accept(pjs, ts_right_brace)) {
                for (;;) {
                    if (_get_token_state(pjs) != ts_string && _get_token_state(pjs) != ts_identifier) {
                        js_throw(pjs, "Expect string or identifier");
                    }
                    _next_token(pjs);
                    _expect(pjs, ts_colon, "Expect :");
                    js_parse_expression(pjs);
                    if (_accept(pjs, ts_comma)) {
                        continue;
                    } else if (_accept(pjs, ts_right_brace)) {
                        break;
                    } else {
                        js_throw(pjs, "Expect , or }");
                    }
                }
            }
            break;
        case ts_function:
            _next_token(pjs);
            _parse_function(pjs);
            break;
        default:
            js_throw(pjs, "Not a value literal");
        }
        return js_undefined();
    }
}

// https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators/Property_accessors
enum _value_accessor_type {
    at_value, // only value, no key or index information, can only get, no put
    at_identifier,
    at_array_member,
    at_object_member,
    at_optional_member
};

struct _value_accessor {
    enum _value_accessor_type type;
    struct js_value v; // value or array or object
    char *p; // identifier head, or object key head
    size_t n; // identifier len, or array index, or object key len
};

// static void _accessor_dump(struct _value_accessor acc) {
//     printf("acc.type= %d\nacc.v= ", acc.type);
//     js_value_dump(acc.v);
//     printf("\nacc.p= %p\nacc.n= %llu\n", acc.p, acc.n);
//     print_hex(acc.p, acc.n);
// }

static void _accessor_put(struct js *pjs, struct _value_accessor acc, struct js_value v) {
    switch (acc.type) {
    case at_value:
        js_throw(pjs, "Can not put value to accessor value type");
    case at_identifier:
        js_variable_assign(pjs, acc.p, acc.n, v);
        return;
    case at_array_member:
        js_array_put(pjs, acc.v, acc.n, v);
        return;
    case at_object_member:
        js_object_put(pjs, acc.v, acc.p, acc.n, v);
        return;
    case at_optional_member:
        if (acc.v.type == vt_object) {
            js_object_put(pjs, acc.v, acc.p, acc.n, v);
        } else {
            js_throw(pjs, "Not object");
        }
        return;
    default:
        js_throw(pjs, "Unknown accessor type");
    }
}

static struct js_value _accessor_get(struct js *pjs, struct _value_accessor acc) {
    switch (acc.type) {
    case at_value:
        return acc.v;
    case at_identifier:
        return js_variable_fetch(pjs, acc.p, acc.n);
    case at_array_member:
        return js_array_get(pjs, acc.v, acc.n);
    case at_object_member:
        return js_object_get(pjs, acc.v, acc.p, acc.n);
    case at_optional_member:
        if (acc.v.type == vt_object) {
            return js_object_get(pjs, acc.v, acc.p, acc.n);
        } else {
            return js_null();
        }
    default:
        js_throw(pjs, "Unknown accessor type");
    }
}

static struct js_value _parse_additive_expression(struct js *pjs);

static struct _value_accessor _parse_accessor(struct js *pjs) {
    if (_require_exec(pjs)) {
        struct _value_accessor acc;
        struct js_value idx;
        size_t tok_idx_backup;
        struct js_value spread;
        size_t i;
        if (_accept(pjs, ts_left_parenthesis)) {
            acc.type = at_value;
            acc.v = js_parse_expression(pjs);
            _expect(pjs, ts_right_parenthesis, "Expect )");
        } else if (_get_token_state(pjs) == ts_identifier) {
            acc.type = at_identifier;
            acc.p = _get_token_head(pjs);
            acc.n = _get_token_length(pjs);
            _next_token(pjs);
        } else {
            acc.type = at_value;
            acc.v = _parse_value(pjs);
        }
        for (;;) {
            if (_accept(pjs, ts_left_bracket)) {
                acc.v = _accessor_get(pjs, acc);
                idx = _parse_additive_expression(pjs);
                if (acc.v.type == vt_array && idx.type == vt_number) {
                    acc.type = at_array_member;
                    acc.n = (size_t)idx.value.number;
                    if (acc.n != idx.value.number) {
                        js_throw(pjs, "Invalid array index, must be positive integer");
                    }
                } else if (acc.v.type == vt_object && idx.type == vt_string) {
                    acc.type = at_object_member;
                    acc.p = idx.value.string->p;
                    acc.n = idx.value.string->len;
                } else {
                    js_throw(pjs, "Must be array[number] or object[string]");
                }
                _expect(pjs, ts_right_bracket, "Expect ]");
            } else if (_accept(pjs, ts_member_access)) {
                acc.v = _accessor_get(pjs, acc);
                if (acc.v.type == vt_object && _get_token_state(pjs) == ts_identifier) {
                    acc.type = at_object_member;
                    acc.p = _get_token_head(pjs);
                    acc.n = _get_token_length(pjs);
                    _next_token(pjs);
                } else {
                    js_throw(pjs, "Must be object.identifier");
                }
            } else if (_accept(pjs, ts_optional_chaining)) {
                acc.v = _accessor_get(pjs, acc);
                if (_get_token_state(pjs) == ts_identifier) {
                    acc.type = at_optional_member;
                    acc.p = _get_token_head(pjs);
                    acc.n = _get_token_length(pjs);
                    _next_token(pjs);
                } else {
                    js_throw(pjs, "Must be value?.identifier");
                }
            } else if (_get_token_state(pjs) == ts_left_parenthesis) {
                acc.v = _accessor_get(pjs, acc);
                _stack_forward(pjs); // record function call stack descriptor '('
                _next_token(pjs);
                if (!_accept(pjs, ts_right_parenthesis)) {
                    for (;;) {
                        if (_accept(pjs, ts_spread)) {
                            spread = js_parse_expression(pjs);
                            if (spread.type != vt_array) {
                                js_throw(pjs, "Operator ... requires array operand");
                            }
                            for (i = 0; i < spread.value.array->len; i++) {
                                js_parameter_push(pjs, js_array_get(pjs, spread, i));
                            }
                        } else {
                            js_parameter_push(pjs, js_parse_expression(pjs));
                        }
                        if (_accept(pjs, ts_comma)) {
                            continue;
                        } else if (_accept(pjs, ts_right_parenthesis)) {
                            break;
                        } else {
                            js_throw(pjs, "Expect , or )");
                        }
                    }
                }
                if (acc.v.type == vt_function) {
                    tok_idx_backup = pjs->tok_cache_idx;
                    pjs->tok_cache_idx = acc.v.value.function->index;
                    // before executing function, put all closure variables into current stack
                    js_value_map_for_each(acc.v.value.function->closure.p, acc.v.value.function->closure.cap, k, kl, v, js_variable_declare(pjs, k, kl, v));
                    _parse_function(pjs);
                    pjs->tok_cache_idx = tok_idx_backup;
                } else if (acc.v.type == vt_cfunction) {
                    acc.v.value.cfunction(pjs);
                } else {
                    js_throw(pjs, "Must be function");
                }
                // js_dump_stack(pjs);
                acc.type = at_value;
                acc.v = pjs->result.type == 0 ? js_null() : pjs->result;
                pjs->result = js_undefined();
                // if return value is function, put all variables in current stack into this function's closure
                if (acc.v.type == vt_function) {
                    struct js_stack_frame *frame = js_stack_peek(pjs);
                    js_value_map_for_each(frame->vars, frame->vars_cap, k, kl, v, js_value_map_put(&(acc.v.value.function->closure.p), &(acc.v.value.function->closure.len), &(acc.v.value.function->closure.cap), k, kl, v));
                }
                js_stack_backward(pjs);
                pjs->mark_return = false;
            } else {
                break;
            }
        }
        return acc;
    } else {
        struct _value_accessor acc = {0};
        if (_accept(pjs, ts_left_parenthesis)) {
            js_parse_expression(pjs);
            _expect(pjs, ts_right_parenthesis, "Expect )");
        } else if (_accept(pjs, ts_identifier)) {
            ;
        } else {
            _parse_value(pjs);
        }
        for (;;) {
            if (_accept(pjs, ts_left_bracket)) {
                _parse_additive_expression(pjs);
                _expect(pjs, ts_right_bracket, "Expect ]");
            } else if (_accept(pjs, ts_member_access)) {
                _expect(pjs, ts_identifier, "Must be object.identifier");
            } else if (_accept(pjs, ts_optional_chaining)) {
                _expect(pjs, ts_identifier, "Must be object.identifier");
            } else if (_accept(pjs, ts_left_parenthesis)) {
                if (!_accept(pjs, ts_right_parenthesis)) {
                    for (;;) {
                        _accept(pjs, ts_spread); // accept or not
                        js_parse_expression(pjs);
                        if (_accept(pjs, ts_comma)) {
                            continue;
                        } else if (_accept(pjs, ts_right_parenthesis)) {
                            break;
                        } else {
                            js_throw(pjs, "Expect , or )");
                        }
                    }
                }
            } else {
                break;
            }
        }
        return acc;
    }
}

static struct js_value _parse_access_call_expression(struct js *pjs) {
    if (_require_exec(pjs)) {
        return _accessor_get(pjs, _parse_accessor(pjs));
    } else {
        _parse_accessor(pjs);
        return js_undefined();
    }
}

static struct js_value _parse_prefix_expression(struct js *pjs) {
    if (_require_exec(pjs)) {
        struct js_value ret;
        double sign = 0;
        if (_accept(pjs, ts_typeof)) {
            ret = _parse_access_call_expression(pjs);
            ret = js_string_sz(pjs, js_value_type_name(ret.type));
        } else if (_accept(pjs, ts_not)) {
            ret = _parse_access_call_expression(pjs);
            if (ret.type != vt_boolean) {
                js_throw(pjs, "Operator ! requires boolean operand");
            }
            ret = js_boolean(!ret.value.boolean);
        } else {
            if (_get_token_state(pjs) == ts_plus) {
                sign = 1;
                _next_token(pjs);
            } else if (_get_token_state(pjs) == ts_minus) {
                sign = -1;
                _next_token(pjs);
            }
            ret = _parse_access_call_expression(pjs);
            if (sign != 0) {
                if (ret.type != vt_number) {
                    js_throw(pjs, "Prefix operators + - require number operand");
                }
                ret = js_number(sign * ret.value.number);
            }
        }
        return ret;
    } else {
        if (_get_token_state(pjs) == ts_typeof || _get_token_state(pjs) == ts_not || _get_token_state(pjs) == ts_plus || _get_token_state(pjs) == ts_minus) {
            _next_token(pjs);
        }
        _parse_access_call_expression(pjs);
        return js_undefined();
    }
}

static struct js_value _parse_multiplicative_expression(struct js *pjs) {
    if (_require_exec(pjs)) {
        struct js_value ret = _parse_prefix_expression(pjs);
        enum js_token_state stat;
        struct js_value val_r;
        while (_get_token_state(pjs) == ts_multiplication || _get_token_state(pjs) == ts_division || _get_token_state(pjs) == ts_mod) {
            stat = _get_token_state(pjs);
            if (ret.type != vt_number) {
                js_throw(pjs, "Operators * / % require left operand be number");
            }
            _next_token(pjs);
            val_r = _parse_prefix_expression(pjs);
            if (val_r.type != vt_number) {
                js_throw(pjs, "Operators * / % require right operand be number");
            }
            if (stat == ts_multiplication) {
                ret = js_number(ret.value.number * val_r.value.number);
            } else if (stat == ts_division) {
                ret = js_number(ret.value.number / val_r.value.number);
            } else {
                ret = js_number(fmod(ret.value.number, val_r.value.number));
            }
        }
        return ret;
    } else {
        _parse_prefix_expression(pjs);
        while (_get_token_state(pjs) == ts_multiplication || _get_token_state(pjs) == ts_division || _get_token_state(pjs) == ts_mod) {
            _next_token(pjs);
            _parse_prefix_expression(pjs);
        }
        return js_undefined();
    }
}

// needed by _parse_accessor()
static struct js_value _parse_additive_expression(struct js *pjs) {
    if (_require_exec(pjs)) {
        struct js_value ret = _parse_multiplicative_expression(pjs);
        enum js_token_state stat;
        struct js_value val_r;
        for (;;) {
            stat = _get_token_state(pjs);
            if (stat == ts_plus) {
                if (ret.type != vt_number && ret.type != vt_string) {
                    js_throw(pjs, "Operator + requires left operand be number or string");
                }
            } else if (stat == ts_minus) {
                if (ret.type != vt_number) {
                    js_throw(pjs, "Operator - requires left operand be number");
                }
            } else {
                break;
            }
            _next_token(pjs);
            val_r = _parse_multiplicative_expression(pjs);
            if (val_r.type != ret.type) {
                js_throw(pjs, "Operators + - require right operand be same type");
            }
            if (stat == ts_plus) {
                if (ret.type == vt_number) {
                    ret = js_number(ret.value.number + val_r.value.number);
                } else {
                    ret = js_string(pjs, ret.value.string->p, ret.value.string->len);
                    string_buffer_append(ret.value.string->p, ret.value.string->len, ret.value.string->cap, val_r.value.string->p, val_r.value.string->len);
                }
            } else {
                ret = js_number(ret.value.number - val_r.value.number);
            }
        }
        return ret;
    } else {
        _parse_multiplicative_expression(pjs);
        while (_get_token_state(pjs) == ts_plus || _get_token_state(pjs) == ts_minus) {
            _next_token(pjs);
            _parse_multiplicative_expression(pjs);
        }
        return js_undefined();
    }
}

static int _strcmp(char *lp, size_t llen, char *rp, size_t rlen) {
    return strncmp(lp, rp, llen > rlen ? llen : rlen);
}

static struct js_value _parse_relational_expression(struct js *pjs) {
    if (_require_exec(pjs)) {
        struct js_value ret = _parse_additive_expression(pjs);
        enum js_token_state stat;
        struct js_value val_r;
        bool eq;
        double num_l, num_r;
        if (_get_token_state(pjs) == ts_equal_to || _get_token_state(pjs) == ts_not_equal_to) {
            stat = _get_token_state(pjs);
            _next_token(pjs);
            val_r = _parse_additive_expression(pjs);
            if (memcmp(&ret, &val_r, sizeof(struct js_value)) == 0) {
                eq = true;
            } else if (ret.type == val_r.type) {
                if (ret.type == vt_number) {
                    eq = ret.value.number == val_r.value.number; // DONT memcmp two double, same value may be different memory content, for example, mod result 0 == 0 may be false perhaps -0 +0
                } else if (ret.type == vt_string) {
                    eq = _strcmp(ret.value.string->p, ret.value.string->len, val_r.value.string->p, val_r.value.string->len) == 0;
                } else {
                    eq = false;
                }
            } else {
                eq = false;
            }
            if (stat == ts_equal_to) {
                ret = js_boolean(eq);
            } else {
                ret = js_boolean(!eq);
            }
        } else if (_get_token_state(pjs) == ts_less_than || _get_token_state(pjs) == ts_less_than_or_equal_to || _get_token_state(pjs) == ts_greater_than || _get_token_state(pjs) == ts_greater_than_or_equal_to) {
            stat = _get_token_state(pjs);
            if (ret.type != vt_number && ret.type != vt_string) {
                js_throw(pjs, "Operators < <= > >= require left operand be number or string");
            }
            _next_token(pjs);
            val_r = _parse_additive_expression(pjs);
            if (val_r.type != ret.type) {
                js_throw(pjs, "Operators < <= > >= require right operand be same type");
            }
            if (ret.type == vt_number) {
                num_l = ret.value.number;
                num_r = val_r.value.number;
            } else {
                num_l = (double)_strcmp(ret.value.string->p, ret.value.string->len, val_r.value.string->p, val_r.value.string->len);
                num_r = 0;
            }
            if (stat == ts_less_than) {
                ret = js_boolean(num_l < num_r);
            } else if (stat == ts_less_than_or_equal_to) {
                ret = js_boolean(num_l <= num_r);
            } else if (stat == ts_greater_than) {
                ret = js_boolean(num_l > num_r);
            } else if (stat == ts_greater_than_or_equal_to) {
                ret = js_boolean(num_l >= num_r);
            } else if (stat == ts_equal_to) {
                ret = js_boolean(num_l == num_r);
            } else {
                ret = js_boolean(num_l != num_r);
            }
        }
        return ret;
    } else {
        _parse_additive_expression(pjs);
        if (_get_token_state(pjs) == ts_equal_to || _get_token_state(pjs) == ts_not_equal_to || _get_token_state(pjs) == ts_less_than || _get_token_state(pjs) == ts_less_than_or_equal_to || _get_token_state(pjs) == ts_greater_than || _get_token_state(pjs) == ts_greater_than_or_equal_to) {
            _next_token(pjs);
            _parse_additive_expression(pjs);
        }
        return js_undefined();
    }
}

static struct js_value _parse_logical_expression(struct js *pjs) {
    if (_require_exec(pjs)) {
        struct js_value ret = _parse_relational_expression(pjs);
        enum js_token_state stat;
        struct js_value val_r;
        while (_get_token_state(pjs) == ts_and || _get_token_state(pjs) == ts_or) {
            stat = _get_token_state(pjs);
            if (ret.type != vt_boolean) {
                js_throw(pjs, "Operators && || require left operand be boolean");
            }
            _next_token(pjs);
            val_r = _parse_relational_expression(pjs);
            if (val_r.type != vt_boolean) {
                js_throw(pjs, "Operators && || require right operand be boolean");
            }
            if (stat == ts_and) {
                ret = js_boolean(ret.value.boolean && val_r.value.boolean);
            } else {
                ret = js_boolean(ret.value.boolean || val_r.value.boolean);
            }
        }
        return ret;
    } else {
        _parse_relational_expression(pjs);
        while (_get_token_state(pjs) == ts_and || _get_token_state(pjs) == ts_or) {
            _next_token(pjs);
            _parse_relational_expression(pjs);
        }
        return js_undefined();
    }
}

// ternary expression as root
struct js_value js_parse_expression(struct js *pjs) {
    if (_require_exec(pjs)) {
        struct js_value ret = _parse_logical_expression(pjs);
        if (_accept(pjs, ts_question)) {
            if (ret.type != vt_boolean) {
                js_throw(pjs, "Operator ?: requires condition operand be boolean");
            }
            if (ret.value.boolean) {
                ret = _parse_logical_expression(pjs);
                _expect(pjs, ts_colon, "Expect :");
                pjs->parse_exec = false;
                _parse_logical_expression(pjs);
                pjs->parse_exec = true;
            } else {
                pjs->parse_exec = false;
                _parse_logical_expression(pjs);
                pjs->parse_exec = true;
                _expect(pjs, ts_colon, "Expect :");
                ret = _parse_logical_expression(pjs);
            }
        }
        return ret;
    } else {
        _parse_logical_expression(pjs);
        if (_accept(pjs, ts_question)) {
            _parse_logical_expression(pjs);
            _expect(pjs, ts_colon, "Expect :");
            _parse_logical_expression(pjs);
        }
        return js_undefined();
    }
}

static void _parse_assignment_expression(struct js *pjs) {
    if (_require_exec(pjs)) {
        struct _value_accessor acc = _parse_accessor(pjs);
        enum js_token_state stat;
        struct js_value val, varval;
        if (_get_token_state(pjs) == ts_assignment || _get_token_state(pjs) == ts_plus_assignment || _get_token_state(pjs) == ts_minus_assignment || _get_token_state(pjs) == ts_multiplication_assignment || _get_token_state(pjs) == ts_division_assignment || _get_token_state(pjs) == ts_mod_assignment) {
            stat = _get_token_state(pjs);
            _next_token(pjs);
            val = js_parse_expression(pjs);
            if (stat == ts_assignment) {
                _accessor_put(pjs, acc, val);
            } else {
                varval = _accessor_get(pjs, acc);
                if (stat == ts_plus_assignment) {
                    if (varval.type != vt_number && varval.type != vt_string) {
                        js_throw(pjs, "Operator += requires left operand be number or string");
                    }
                } else {
                    if (varval.type != vt_number) {
                        js_throw(pjs, "Operators -= *= /= %= require left operand be number");
                    }
                }
                if (val.type != varval.type) {
                    js_throw(pjs, "Operators += -= *= /= %= require right operand be same type");
                }
                if (stat == ts_plus_assignment) {
                    if (varval.type == vt_number) {
                        varval = js_number(varval.value.number + val.value.number);
                    } else {
                        varval = js_string(pjs, varval.value.string->p, varval.value.string->len);
                        string_buffer_append(varval.value.string->p, varval.value.string->len, varval.value.string->cap, val.value.string->p, val.value.string->len);
                    }
                } else if (stat == ts_minus_assignment) {
                    varval = js_number(varval.value.number - val.value.number);
                } else if (stat == ts_multiplication_assignment) {
                    varval = js_number(varval.value.number * val.value.number);
                } else if (stat == ts_division_assignment) {
                    varval = js_number(varval.value.number / val.value.number);
                } else { // ts_mod_assignment
                    varval = js_number(fmod(varval.value.number, val.value.number));
                }
                _accessor_put(pjs, acc, varval);
            }
        } else if (_get_token_state(pjs) == ts_plus_plus || _get_token_state(pjs) == ts_minus_minus) {
            varval = _accessor_get(pjs, acc);
            if (varval.type != vt_number) {
                js_throw(pjs, "Operators ++ -- require operand be number");
            }
            if (_get_token_state(pjs) == ts_plus_plus) {
                varval = js_number(varval.value.number + 1);
            } else {
                varval = js_number(varval.value.number - 1);
            }
            _accessor_put(pjs, acc, varval);
            _next_token(pjs);
        }
    } else {
        _parse_accessor(pjs);
        if (_get_token_state(pjs) == ts_assignment || _get_token_state(pjs) == ts_plus_assignment || _get_token_state(pjs) == ts_minus_assignment || _get_token_state(pjs) == ts_multiplication_assignment || _get_token_state(pjs) == ts_division_assignment || _get_token_state(pjs) == ts_mod_assignment) {
            _next_token(pjs);
            js_parse_expression(pjs);
        } else if (_get_token_state(pjs) == ts_plus_plus || _get_token_state(pjs) == ts_minus_minus) {
            _next_token(pjs);
        }
    }
}

static void _parse_declaration_expression(struct js *pjs) {
    if (_require_exec(pjs)) {
        char *ident_h;
        size_t ident_len;
        _expect(pjs, ts_let, "Expect let");
        for (;;) {
            if (_get_token_state(pjs) != ts_identifier) {
                js_throw(pjs, "Expect variable name");
            }
            ident_h = _get_token_head(pjs);
            ident_len = _get_token_length(pjs);
            _next_token(pjs);
            if (_accept(pjs, ts_assignment)) {
                js_variable_declare(pjs, ident_h, ident_len, js_parse_expression(pjs));
            } else {
                js_variable_declare(pjs, ident_h, ident_len, js_null());
            }
            if (_accept(pjs, ts_comma)) {
                continue;
            } else {
                break;
            }
        }
    } else {
        _expect(pjs, ts_let, "Expect let");
        for (;;) {
            _expect(pjs, ts_identifier, "Expect variable name");
            if (_accept(pjs, ts_assignment)) {
                js_parse_expression(pjs);
            }
            if (_accept(pjs, ts_comma)) {
                continue;
            } else {
                break;
            }
        }
    }
}

// needed by _parse_function()
static void _parse_statement(struct js *pjs) {
    enum { classic_for,
           for_in,
           for_of } for_type;
    if (_require_exec(pjs)) {
        struct js_value cond = js_undefined(), val;
        size_t tok_idx_backup, tok_idx_backup_2, tok_idx_backup_3;
        struct _value_accessor acc;
        int loop_count;
        char *ident_h;
        size_t ident_len;
        if (_accept(pjs, ts_semicolon)) {
            ;
        } else if (_get_token_state(pjs) == ts_left_brace) { // DONT use _accept, _stack_forward will record token
            _stack_forward(pjs);
            _next_token(pjs);
            while (_get_token_state(pjs) != ts_right_brace) {
                _parse_statement(pjs);
            }
            js_stack_backward(pjs);
            _next_token(pjs);
        } else if (_accept(pjs, ts_if)) {
            _expect(pjs, ts_left_parenthesis, "Expect (");
            cond = js_parse_expression(pjs);
            if (cond.type != vt_boolean) {
                js_throw(pjs, "Condition must be boolean");
            }
            _expect(pjs, ts_right_parenthesis, "Expect )");
            pjs->parse_exec = cond.value.boolean;
            _parse_statement(pjs);
            if (_accept(pjs, ts_else)) {
                pjs->parse_exec = !cond.value.boolean;
                _parse_statement(pjs);
            }
            pjs->parse_exec = true; // restore execute
        } else if (_accept(pjs, ts_while)) {
            _expect(pjs, ts_left_parenthesis, "Expect (");
            tok_idx_backup = pjs->tok_cache_idx; // backup token
            while (pjs->parse_exec && !pjs->mark_break) {
                pjs->tok_cache_idx = tok_idx_backup; // restore token
                cond = js_parse_expression(pjs);
                if (cond.type != vt_boolean) {
                    js_throw(pjs, "Condition must be boolean");
                }
                _expect(pjs, ts_right_parenthesis, "Expect )");
                pjs->parse_exec = cond.value.boolean;
                _parse_statement(pjs); // skip if cond == false
                pjs->mark_continue = false; // if not set to false, next cond may be NULL
            }
            pjs->parse_exec = true; // restore execute
            pjs->mark_break = false;
            pjs->mark_continue = false;
        } else if (_accept(pjs, ts_do)) {
            tok_idx_backup = pjs->tok_cache_idx;
            do {
                pjs->tok_cache_idx = tok_idx_backup;
                _parse_statement(pjs);
                pjs->mark_continue = false; // https://stackoverflow.com/questions/64120214/continue-statement-in-a-do-while-loop
                _expect(pjs, ts_while, "Expect while");
                _expect(pjs, ts_left_parenthesis, "Expect (");
                cond = js_parse_expression(pjs);
                if (cond.type != 0 && cond.type != vt_boolean) { // may be NULL due to 'break' mark
                    js_throw(pjs, "Condition must be boolean");
                }
                _expect(pjs, ts_right_parenthesis, "Expect )");
            } while (cond.type != 0 && cond.value.boolean && !pjs->mark_break);
            pjs->mark_break = false;
            pjs->mark_continue = false;
            _expect(pjs, ts_semicolon, "Expect ;");
        } else if (_get_token_state(pjs) == ts_for) { // DONT use _accept, _stack_forward will record token
            _stack_forward(pjs);
            _next_token(pjs);
            _expect(pjs, ts_left_parenthesis, "Expect (");
            if (_accept(pjs, ts_let)) {
                acc = _parse_accessor(pjs);
                if (acc.type != at_identifier) {
                    js_throw(pjs, "Expect identifier");
                }
                if (_accept(pjs, ts_assignment)) {
                    js_variable_declare(pjs, acc.p, acc.n, js_parse_expression(pjs));
                    _expect(pjs, ts_semicolon, "Expect ;");
                    for_type = classic_for;
                } else if (_accept(pjs, ts_in)) {
                    js_variable_declare(pjs, acc.p, acc.n, js_null());
                    for_type = for_in;
                } else if (_accept(pjs, ts_of)) {
                    js_variable_declare(pjs, acc.p, acc.n, js_null());
                    for_type = for_of;
                } else {
                    js_throw(pjs, "Unknown for loop type");
                }
            } else if (_accept(pjs, ts_semicolon)) {
                for_type = classic_for;
            } else {
                acc = _parse_accessor(pjs);
                if (_accept(pjs, ts_assignment)) {
                    _accessor_put(pjs, acc, js_parse_expression(pjs));
                    _expect(pjs, ts_semicolon, "Expect ;");
                    for_type = classic_for;
                } else if (_accept(pjs, ts_in)) {
                    for_type = for_in;
                } else if (_accept(pjs, ts_of)) {
                    for_type = for_of;
                } else {
                    js_throw(pjs, "Unknown for loop type");
                }
            }
            // printf("for_type= %d\n", for_type);
            if (for_type == classic_for) {
                tok_idx_backup = pjs->tok_cache_idx;
                while (pjs->parse_exec && !pjs->mark_break) {
                    pjs->tok_cache_idx = tok_idx_backup;
                    if (_accept(pjs, ts_semicolon)) {
                        cond = js_boolean(true);
                    } else {
                        cond = js_parse_expression(pjs);
                        if (cond.type != vt_boolean) {
                            js_throw(pjs, "Condition must be boolean");
                        }
                        _expect(pjs, ts_semicolon, "Expect ;");
                    }
                    tok_idx_backup_2 = pjs->tok_cache_idx;
                    pjs->parse_exec = false; // first, skip the 3rd part
                    if (!_accept(pjs, ts_right_parenthesis)) {
                        _parse_assignment_expression(pjs);
                        _expect(pjs, ts_right_parenthesis, "Expect )");
                    }
                    pjs->parse_exec = cond.value.boolean;
                    _parse_statement(pjs);
                    pjs->mark_continue = false;
                    tok_idx_backup_3 = pjs->tok_cache_idx;
                    pjs->tok_cache_idx = tok_idx_backup_2; // then, exec the 3rd pard
                    if (!_accept(pjs, ts_right_parenthesis)) {
                        _parse_assignment_expression(pjs);
                    }
                    pjs->tok_cache_idx = tok_idx_backup_3; // restore end position or will stay at ')' when leaving while loop, will cause next statement parse error 'ts_right_parenthesis:): Expect identifier'
                }
                pjs->parse_exec = true;
                pjs->mark_break = false;
                pjs->mark_continue = false;
            } else {
                val = _parse_access_call_expression(pjs);
                _expect(pjs, ts_right_parenthesis, "Expect )");
                tok_idx_backup = pjs->tok_cache_idx;
                loop_count = 0;
                if (val.type == vt_array) {
                    js_value_list_for_each(val.value.array->p, val.value.array->len, i, v, {
                        if (v.type != vt_null) { // skip null
                            pjs->tok_cache_idx = tok_idx_backup;
                            if (for_type == for_in) {
                                _accessor_put(pjs, acc, js_number((double)i));
                            } else {
                                _accessor_put(pjs, acc, v);
                            }
                            _parse_statement(pjs);
                            loop_count++;
                            pjs->mark_continue = false;
                            if (pjs->mark_break) {
                                break;
                            }
                        }
                    });
                    pjs->mark_break = false;
                } else if (val.type == vt_object) {
                    js_value_map_for_each(val.value.object->p, val.value.object->cap, k, kl, v, {
                        if (v.type != vt_null) { // skip null
                            pjs->tok_cache_idx = tok_idx_backup;
                            if (for_type == for_in) {
                                _accessor_put(pjs, acc, js_string(pjs, k, kl));
                            } else {
                                _accessor_put(pjs, acc, v);
                            }
                            _parse_statement(pjs);
                            loop_count++;
                            pjs->mark_continue = false;
                            if (pjs->mark_break) {
                                break;
                            }
                        }
                    });
                    pjs->mark_break = false;
                } else {
                    js_throw(pjs, "For in/of loop require array or object");
                }
                if (!loop_count) { // if loop didn't happen, still should walk through loop body
                    pjs->parse_exec = false;
                    _parse_statement(pjs);
                    pjs->parse_exec = true;
                }
            }
            js_stack_backward(pjs);
            // puts("****** END OF FOR ******");
            // js_dump_stack(pjs);
        } else if (_accept(pjs, ts_break)) {
            pjs->mark_break = true;
            _expect(pjs, ts_semicolon, "Expect ;");
        } else if (_accept(pjs, ts_continue)) {
            pjs->mark_continue = true;
            _expect(pjs, ts_semicolon, "Expect ;");
        } else if (_get_token_state(pjs) == ts_function) {
            _expect(pjs, ts_function, "Expect 'function'");
            if (_get_token_state(pjs) != ts_identifier) {
                js_throw(pjs, "Expect function name");
            }
            ident_h = _get_token_head(pjs);
            ident_len = _get_token_length(pjs);
            _next_token(pjs);
            js_variable_declare(pjs, ident_h, ident_len, js_function(pjs, pjs->tok_cache_idx));
            // only walk through, no exec
            pjs->parse_exec = false;
            _parse_function(pjs);
            pjs->parse_exec = true;
        } else if (_accept(pjs, ts_return)) {
            if (!_accept(pjs, ts_semicolon)) {
                pjs->result = js_parse_expression(pjs);
                _expect(pjs, ts_semicolon, "Expect ;");
            }
            pjs->mark_return = true;
        } else if (_accept(pjs, ts_delete)) {
            if (_get_token_state(pjs) == ts_identifier) {
                js_variable_erase(pjs, _get_token_head(pjs), _get_token_length(pjs));
            } else {
                js_throw(pjs, "Expect identifier");
            }
            _next_token(pjs);
            _expect(pjs, ts_semicolon, "Expect ;");
        } else if (_get_token_state(pjs) == ts_let) {
            _parse_declaration_expression(pjs);
            _expect(pjs, ts_semicolon, "Expect ;");
            // puts("****** END OF LET ******");
            // js_dump_stack(pjs);
        } else {
            _parse_assignment_expression(pjs);
            _expect(pjs, ts_semicolon, "Expect ;");
        }
    } else {
        if (_accept(pjs, ts_semicolon)) {
            ;
        } else if (_accept(pjs, ts_left_brace)) {
            while (_get_token_state(pjs) != ts_right_brace) {
                _parse_statement(pjs);
            }
            _next_token(pjs);
        } else if (_accept(pjs, ts_if)) {
            _expect(pjs, ts_left_parenthesis, "Expect (");
            js_parse_expression(pjs);
            _expect(pjs, ts_right_parenthesis, "Expect )");
            _parse_statement(pjs);
            if (_accept(pjs, ts_else)) {
                _parse_statement(pjs);
            }
        } else if (_accept(pjs, ts_while)) {
            _expect(pjs, ts_left_parenthesis, "Expect (");
            js_parse_expression(pjs);
            _expect(pjs, ts_right_parenthesis, "Expect )");
            _parse_statement(pjs);
        } else if (_accept(pjs, ts_do)) {
            _parse_statement(pjs);
            _expect(pjs, ts_while, "Expect while");
            _expect(pjs, ts_left_parenthesis, "Expect (");
            js_parse_expression(pjs);
            _expect(pjs, ts_right_parenthesis, "Expect )");
            _expect(pjs, ts_semicolon, "Expect ;");
        } else if (_accept(pjs, ts_for)) {
            _expect(pjs, ts_left_parenthesis, "Expect (");
            if (_accept(pjs, ts_let)) {
                _expect(pjs, ts_identifier, "Expect identifier");
                if (_accept(pjs, ts_assignment)) {
                    js_parse_expression(pjs);
                    _expect(pjs, ts_semicolon, "Expect ;");
                    for_type = classic_for;
                } else if (_accept(pjs, ts_in)) {
                    for_type = for_in;
                } else if (_accept(pjs, ts_of)) {
                    for_type = for_of;
                } else {
                    js_throw(pjs, "Unknown for loop type");
                }
            } else if (_accept(pjs, ts_semicolon)) {
                for_type = classic_for;
            } else {
                _parse_accessor(pjs);
                if (_accept(pjs, ts_assignment)) {
                    js_parse_expression(pjs);
                    _expect(pjs, ts_semicolon, "Expect ;");
                    for_type = classic_for;
                } else if (_accept(pjs, ts_in)) {
                    for_type = for_in;
                } else if (_accept(pjs, ts_of)) {
                    for_type = for_of;
                } else {
                    js_throw(pjs, "Unknown for loop type");
                }
            }
            if (for_type == classic_for) {
                if (!_accept(pjs, ts_semicolon)) {
                    js_parse_expression(pjs);
                    _expect(pjs, ts_semicolon, "Expect ;");
                }
                if (!_accept(pjs, ts_right_parenthesis)) {
                    _parse_assignment_expression(pjs);
                    _expect(pjs, ts_right_parenthesis, "Expect )");
                }
                _parse_statement(pjs);
            } else {
                _parse_access_call_expression(pjs);
                _expect(pjs, ts_right_parenthesis, "Expect )");
                _parse_statement(pjs);
            }
        } else if (_accept(pjs, ts_break)) {
            _expect(pjs, ts_semicolon, "Expect ;");
        } else if (_accept(pjs, ts_continue)) {
            _expect(pjs, ts_semicolon, "Expect ;");
        } else if (_get_token_state(pjs) == ts_function) {
            _expect(pjs, ts_function, "Expect 'function'");
            _expect(pjs, ts_identifier, "Expect function name");
            _parse_function(pjs);
        } else if (_accept(pjs, ts_return)) {
            if (!_accept(pjs, ts_semicolon)) {
                js_parse_expression(pjs);
                _expect(pjs, ts_semicolon, "Expect ;");
            }
        } else if (_accept(pjs, ts_delete)) {
            _expect(pjs, ts_identifier, "Expect identifier");
            _expect(pjs, ts_semicolon, "Expect ;");
        } else if (_get_token_state(pjs) == ts_let) {
            _parse_declaration_expression(pjs);
            _expect(pjs, ts_semicolon, "Expect ;");
        } else {
            _parse_assignment_expression(pjs);
            _expect(pjs, ts_semicolon, "Expect ;");
        }
    }
}

void js_parse_script(struct js *pjs) {
    while (_get_token_state(pjs) != ts_end_of_file) {
        _parse_statement(pjs);
    }
}

void js_parser_print_error(struct js *pjs) {
    // Here, cache index may exceeds cache boundary, so DONT use token head and length
    // printf("%d %p\n", (int)_get_token_length(pjs), _get_token_head(pjs));
    printf("%s:%ld %ld:%s: %s\n", pjs->err_file, pjs->err_line,
           _get_token_line(pjs), js_token_state_name(_get_token_state(pjs)), pjs->err_msg);
}
