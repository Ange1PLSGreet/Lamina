#include "symbolic.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <functional>
#include <iostream>

// 符号表达式的化简实现
std::shared_ptr<SymbolicExpr> SymbolicExpr::simplify() const {
    switch (type) {
        case Type::Number:
        case Type::Variable:
            return std::make_shared<SymbolicExpr>(*this);
            
        case Type::Sqrt:
            return simplify_sqrt();
            
        case Type::Multiply:
            return simplify_multiply();
            
        case Type::Add:
            return simplify_add();

        case Type::Power:
            return simplify_power();
        
        default:
            return std::make_shared<SymbolicExpr>(*this);
    }
}

std::shared_ptr<SymbolicExpr> SymbolicExpr::simplify_sqrt() const {
    if (operands.empty()) return std::make_shared<SymbolicExpr>(*this);
    
    auto simplified_operand = operands[0]->simplify();
    
    if (simplified_operand->is_number()) {
		
		auto scvrs = simplified_operand->convert_rational();
		
		if (simplified_operand->is_rational() && scvrs.get_denominator() == ::BigInt(1)) {
			::BigInt actual = scvrs.get_numerator();
			simplified_operand->number_value = actual;
		}
		
        auto num_val = simplified_operand->get_number();
        if (std::holds_alternative<int>(num_val)) {
            int n = std::get<int>(num_val);
            if (n < 0) throw std::runtime_error("Square root of negative number");
            if (n == 0 || n == 1) return SymbolicExpr::number(n);
            int sqrt_n = static_cast<int>(std::sqrt(n));
            if (sqrt_n * sqrt_n == n) return SymbolicExpr::number(sqrt_n);
            int factor = 1, remaining = n;
            for (int i = 2; i * i <= remaining; ++i) {
                while (remaining % (i * i) == 0) {
                    factor *= i;
                    remaining /= (i * i);
                }
            }
            if (factor > 1) {
                if (remaining == 1) return SymbolicExpr::number(factor);
                else return SymbolicExpr::multiply(SymbolicExpr::number(factor), SymbolicExpr::sqrt(SymbolicExpr::number(remaining)));
            }
        }
        if (std::holds_alternative<::BigInt>(num_val)) {
            const auto& bi = std::get<::BigInt>(num_val);
            if (bi.negative) throw std::runtime_error("Square root of negative number");
            if (bi.is_zero() || bi == BigInt(1)) return SymbolicExpr::number(bi);
            if (bi.is_perfect_square()) return SymbolicExpr::number(bi.sqrt());
            // TODO: 这个等BigInt效率高点再说，或者换个算法
            /*BigInt factor(1), remaining(bi);
            for (BigInt i(2); i * i <= remaining; i = i + BigInt(1)) {
                while ((remaining % (i * i)).is_zero()) {
                    factor = factor * i;
                    remaining = remaining / (i * i);
                }
            }
            if (factor > BigInt(1)) {
                if (remaining == BigInt(1)) return SymbolicExpr::number(factor);
                else return SymbolicExpr::multiply(SymbolicExpr::number(factor), SymbolicExpr::sqrt(SymbolicExpr::number(remaining)));
            }*/
            // 暂时只判断可以转成int的
            if (bi <= BigInt(INT_MAX) && bi >= BigInt(INT_MIN)) {
                return SymbolicExpr::sqrt(SymbolicExpr::number(bi.to_int()))->simplify();
            }
        }
		// TODO:实现分数sqrt化简
    }
    // sqrt(x*x) 或 sqrt(π*π) 直接返回 x 或 π
    if (simplified_operand->type == SymbolicExpr::Type::Multiply && simplified_operand->operands.size() == 2) {
        const auto& a = simplified_operand->operands[0];
        const auto& b = simplified_operand->operands[1];
        // sqrt(x*x) = x
        if (a->type == SymbolicExpr::Type::Variable && b->type == SymbolicExpr::Type::Variable && a->identifier == b->identifier) {
            return a;
        }
        // sqrt(π*π) = π
        if (a->type == SymbolicExpr::Type::Variable && b->type == SymbolicExpr::Type::Variable &&
            ((a->identifier == "π" && b->identifier == "π") || (a->identifier == "pi" && b->identifier == "pi"))) {
            return a;
        }
        if (a->to_string() == b->to_string()) {
            return a;
        }
        auto get_var = [](const std::shared_ptr<SymbolicExpr>& expr) -> std::string {
            if (expr->type == SymbolicExpr::Type::Variable) return expr->identifier;
            if (expr->type == SymbolicExpr::Type::Multiply && expr->operands.size() == 2) {
                if (expr->operands[1]->type == SymbolicExpr::Type::Variable) return expr->operands[1]->identifier;
            }
            return "";
        };
        std::string var_a = get_var(a);
        std::string var_b = get_var(b);
        if (!var_a.empty() && var_a == var_b) {
            // sqrt((c*π)*(d*π)) = sqrt((c*d)*π^2) = π*sqrt(c*d)
            auto pow2 = SymbolicExpr::power(SymbolicExpr::variable(var_a), SymbolicExpr::number(2));
            auto left_coeff = (a->type == SymbolicExpr::Type::Multiply && a->operands[0]->is_number()) ? a->operands[0] : SymbolicExpr::number(1);
            auto right_coeff = (b->type == SymbolicExpr::Type::Multiply && b->operands[0]->is_number()) ? b->operands[0] : SymbolicExpr::number(1);
            auto coeff_mul = SymbolicExpr::multiply(left_coeff, right_coeff)->simplify();
            // sqrt((c*π)*(d*π)) = π*sqrt(c*d)
            if (coeff_mul->is_number()) {
                auto sqrt_coeff = SymbolicExpr::sqrt(coeff_mul)->simplify();
                if (sqrt_coeff->is_number() && sqrt_coeff->to_string() == "1") {
                    return SymbolicExpr::variable(var_a);
                } else {
                    return SymbolicExpr::multiply(sqrt_coeff, SymbolicExpr::variable(var_a));
                }
            } else {
                // fallback: sqrt(π^2)
                return SymbolicExpr::sqrt(pow2)->simplify();
            }
        }
    }
    // sqrt(x^2) 或 sqrt(π^2) 直接返回 x 或 π
    if (simplified_operand->type == SymbolicExpr::Type::Power && simplified_operand->operands.size() == 2) {
        const auto& base = simplified_operand->operands[0];
        const auto& exp = simplified_operand->operands[1];
        if (exp->is_number()) {
            auto exp_val = exp->get_number();
            int n = 0;
            BigInt big_n;// 0 for default
            if (std::holds_alternative<int>(exp_val)) n = std::get<int>(exp_val);
            else if (std::holds_alternative<::Rational>(exp_val)) {
                ::Rational r = std::get<::Rational>(exp_val);
 
                if (r.is_integer()) big_n = r.get_numerator();
            }
            if (n == 2 || big_n.to_string() == "2") {
                // sqrt(x^2) = x
                if (base->type == SymbolicExpr::Type::Variable && (base->identifier == "π" || base->identifier == "pi")) {
                    return base;
                }
                return base;
            }
        }
    }
    return SymbolicExpr::sqrt(simplified_operand);
}

std::shared_ptr<SymbolicExpr> SymbolicExpr::simplify_multiply() const {
    if (operands.size() != 2) return std::make_shared<SymbolicExpr>(*this);
    
    auto left = operands[0]->simplify();
    auto right = operands[1]->simplify();
	
	// TODO: Debug output:
	std::cerr << "[Debug output] Init: Processing l:" << left->to_string() << ", r:" << right->to_string() << std::endl;
	
	// TODO: 1 乘以某个内容，直接返回另一边
	
    // 如果两个操作数都是数字，直接相乘
    if (left->is_number() && right->is_number()) {
        auto left_num = left->get_number();
        auto right_num = right->get_number();
        
        if (std::holds_alternative<int>(left_num) && std::holds_alternative<int>(right_num)) {
            int result = std::get<int>(left_num) * std::get<int>(right_num);
            return SymbolicExpr::number(result);
        } else {
			::Rational result = left->convert_rational() * right->convert_rational();
			return SymbolicExpr::number(result);
		}
    }
	
	// 加法运算特殊化简
	if ((left->type == SymbolicExpr::Type::Add) || (right->type == SymbolicExpr::Type::Add)) {
		
		auto res = SymbolicExpr::number(0);
		
		if ((left->type == SymbolicExpr::Type::Add) && (right->type == SymbolicExpr::Type::Add)) {
			for (auto &i : left->operands) {
				for (auto &j : right->operands) {
					res = SymbolicExpr::add(res, SymbolicExpr::multiply(i, j)->simplify())->simplify();
				}
			}
		} else {
			if (left->type != SymbolicExpr::Type::Add)
				std::swap(left, right);
			
			for (auto &i : left->operands) {
				res = SymbolicExpr::add(res, SymbolicExpr::multiply(i, right)->simplify())->simplify();
			}
		}

		return res;
		
	}
	
	auto is_power_compatible = [](const std::shared_ptr<SymbolicExpr>& expr) -> bool {
		return expr->type == SymbolicExpr::Type::Number || expr->type == SymbolicExpr::Type::Sqrt
			|| expr->type == SymbolicExpr::Type::Power || expr->type == SymbolicExpr::Type::Variable;
	};
	
	// 注意，除法使用指数
	auto power_compatible = [](const std::shared_ptr<SymbolicExpr>& expr) -> std::shared_ptr<SymbolicExpr> {
		if (expr->type == SymbolicExpr::Type::Number || expr->type == SymbolicExpr::Type::Variable) {
			return SymbolicExpr::power(expr, SymbolicExpr::number(1));
		} else if (expr->type == SymbolicExpr::Type::Sqrt) {
			return SymbolicExpr::power(expr, SymbolicExpr::number(::Rational(1, 2)));
		} else if (expr->type == SymbolicExpr::Type::Power) {
			return expr;
		} else {
			return expr;
		}
	};
	
	auto is_compounded_sqrt = [](const std::shared_ptr<SymbolicExpr>& expr) -> bool {
		if (expr->type == SymbolicExpr::Type::Multiply) {
			if (expr->operands.size() >= 2 && (expr->operands[0]->type == SymbolicExpr::Type::Number)
				&& (expr->operands[1]->type == SymbolicExpr::Type::Sqrt)) return true;
		}
		return false;
	};
	
	auto is_for_auxiliary = [is_compounded_sqrt](const std::shared_ptr<SymbolicExpr> &obj) -> bool {
		// 判断是否可用于 sqrt_and_auxiliary，如果不可用，直接尝试 flatten multiply
		return (obj->type == SymbolicExpr::Type::Number) || (obj->type == SymbolicExpr::Type::Sqrt)
			|| is_compounded_sqrt(obj);
	};
	
	// TODO: 和指数类型乘法合并/简化
	auto sqrt_and_auxiliary = [is_power_compatible, power_compatible, is_compounded_sqrt]
		(const std::shared_ptr<SymbolicExpr> &obj, bool no_simplify = false) -> std::shared_ptr<SymbolicExpr> {
		
		// TODO: Debug output:
		std::cerr << "[Debug output] Starting sqrt-and-aux process" << std::endl;
		
		// 此处可以考虑优化
		auto left = no_simplify ? obj->operands[0] : obj->operands[0]->simplify();
		auto right = no_simplify ? obj->operands[1] : obj->operands[1]->simplify();
		
		// TODO: Debug output:
		std::cerr << "[Debug output] Processing l:" << left->to_string() << ", r:" << right->to_string() << std::endl;
		
		// 根式相乘的一般处理
		if (left->type == SymbolicExpr::Type::Sqrt || is_compounded_sqrt(left)
			|| right->type == SymbolicExpr::Type::Sqrt || is_compounded_sqrt(right)) {
			
			if ((!is_compounded_sqrt(left)) && (!is_compounded_sqrt(right))) {
				
				if (right->type == SymbolicExpr::Type::Sqrt)
					std::swap(left, right);
				
				// TODO: Debug output:
				std::cerr << "[Debug output] [4] Starting sqrt-and-aux process" << std::endl;
				
				bool negative = false;
				if (left->type == SymbolicExpr::Type::Number && left->convert_rational() < ::Rational(0, 1))
					negative = !negative;
				if (right->type == SymbolicExpr::Type::Number && right->convert_rational() < ::Rational(0, 1))
					negative = !negative;
				
				std::shared_ptr<SymbolicExpr> sresult = SymbolicExpr::multiply(
					(left->type == SymbolicExpr::Type::Number) ? (SymbolicExpr::multiply(left, left)->simplify())
																: left->operands[0],
					(right->type == SymbolicExpr::Type::Number) ? (SymbolicExpr::multiply(right, right)->simplify())
																: right->operands[0]);
				
				auto res = SymbolicExpr::sqrt(sresult)->simplify();
				
				if (negative)
					return SymbolicExpr::multiply(SymbolicExpr::number(-1), res);
				else
					return res;
				
			} else {
				
				// 把右侧加入到左侧
				if (!is_compounded_sqrt(left))
					std::swap(left, right);
				
				// TODO: Debug output:
				std::cerr << "[Debug output] [5] Starting sqrt-and-aux process" << std::endl;
				
				bool negative = false;
				
				auto res = std::make_shared<SymbolicExpr>(*left);
				auto simplify_res = [&]() {
					if (is_compounded_sqrt(res->operands[1])) {
						res->operands[0] = SymbolicExpr::multiply(res->operands[0], res->operands[1]->operands[0])->simplify();
						res->operands[1] = res->operands[1]->operands[1];		// 不用再 sqrt
					}
					res->simplify();
				};
				
				if (right->type == SymbolicExpr::Type::Number) {
					if (right->convert_rational() < ::Rational(0, 1)) {
						negative = true;
					}
					res->operands[0] = SymbolicExpr::multiply(res->operands[0], right)->simplify();
				} else if (right->type == SymbolicExpr::Type::Sqrt) {
					res->operands[1] = SymbolicExpr::multiply(res->operands[1], right)->simplify();
					simplify_res();
				} else if (is_compounded_sqrt(right)) {
					res->operands[0] = SymbolicExpr::multiply(res->operands[0], right->operands[0])->simplify();
					res->operands[1] = SymbolicExpr::multiply(res->operands[1], right->operands[1])->simplify();
					simplify_res();
				}
				
				if (negative) {
					if (res->type == SymbolicExpr::Type::Multiply) {
						// 这个需要化简
						res->operands[0] = SymbolicExpr::multiply(SymbolicExpr::number(-1), res->operands[0])->simplify();
					} else {
						// 不要再化简
						res = SymbolicExpr::multiply(SymbolicExpr::number(-1), res);
					}
				}
				
				return res;
				
			}
			
		}
		
		// 回退到保留乘法
		return SymbolicExpr::multiply(left, right);
	};

	
	// 指数类型相乘的处理（可能考虑合并相同指数项）
	if ((left->type == SymbolicExpr::Type::Power || right->type == SymbolicExpr::Type::Power
		|| (left->type == right->type && left->type == SymbolicExpr::Type::Variable))
		|| (!is_for_auxiliary(left)) || (!is_for_auxiliary(right))) {
		
		if (is_power_compatible(left) && is_power_compatible(right)) {
			if (left->type != SymbolicExpr::Type::Power)
				std::swap(left, right);
			
			auto lcom = left;
			if (left->type != SymbolicExpr::Type::Power)
				lcom = power_compatible(left);
			
			auto rcom = power_compatible(right);
			
			// TODO: 提升为成员函数，判断相等
			std::function<bool(std::shared_ptr<SymbolicExpr>,std::shared_ptr<SymbolicExpr>)> is_power_equiv;
			is_power_equiv = [&](std::shared_ptr<SymbolicExpr> a, std::shared_ptr<SymbolicExpr> b) -> bool {
				if (a->type != b->type) 
					return false;
				if (a->type == SymbolicExpr::Type::Number)
					return a->convert_rational() == b->convert_rational();
				else if (a->type == SymbolicExpr::Type::Sqrt)
					return is_power_equiv(a->operands[0], b->operands[0]);
				else if (a->type == SymbolicExpr::Type::Variable)
					return a->identifier == b->identifier;
				else
					return false;
			};
			// TODO: Debug output:
			std::cerr << "[Debug output] [1] preparing to merge exponents" << std::endl;
			
			if (lcom->operands[1]->is_number() && rcom->operands[1]->is_number()) {
				
				auto lcr = lcom->operands[1]->convert_rational();
				auto rcr = rcom->operands[1]->convert_rational();
				
				auto ldr = lcr.get_denominator();
				auto rdr = rcr.get_denominator();
				
				if (is_power_equiv(lcom->operands[0], rcom->operands[0])) {
					// TODO: Debug output:
					std::cerr << "[Debug output] [1] Merging bases" << std::endl;
					return SymbolicExpr::power(lcom->operands[0], SymbolicExpr::add(lcom->operands[1], rcom->operands[1]))->simplify();
				}
				
				if (ldr == rdr) {
					if (lcr == rcr) {
						// TODO: Debug output:
						std::cerr << "[Debug output] [1a] Merging exponents in a simplified way" << std::endl;
						return SymbolicExpr::power(SymbolicExpr::multiply(lcom->operands[0], rcom->operands[0]),
								SymbolicExpr::number(lcr))->simplify();
					} else if (ldr == ::BigInt(1)) {
						// 没有分母（特殊处理，避免死循环）；此处，底数、指数均不能合并。
						// 此处不应再对整体进行 multiply 的化简调用。
						// 留给以后实现。
						// TODO: Debug output:
						std::cerr << "[Debug output] [1b] Give up merging" << std::endl;
					} else {
						// 注意这里的 multiply 不化简（power 的处理要跟上）
						// TODO: Debug output:
						std::cerr << "[Debug output] [1c] Merging exponents" << std::endl;
						auto new_base = SymbolicExpr::multiply(
								SymbolicExpr::power(lcom->operands[0], SymbolicExpr::number(lcr.get_numerator())),
								SymbolicExpr::power(rcom->operands[0], SymbolicExpr::number(rcr.get_numerator())));
						// 进行简化处理，同时避免死循环
						// 执行外部的 simplify 时内部的是不必要的
						new_base = new_base->simplify(); // new_base = sqrt_and_auxiliary(new_base, true);
						return SymbolicExpr::power(new_base, SymbolicExpr::number(::Rational(::BigInt(1), lcr.get_denominator())))->simplify();
					}
				}
			}
			
			// TODO: Debug output:
			std::cerr << "[Debug output] End of power-compatible process" << std::endl;
			
		} else {
			
			// 开始尝试 flatten
			std::function<bool(const std::shared_ptr<SymbolicExpr>&)> flatten_multiply;
			// 暂时只支持有理指数化简
			// 键为底数的值，值为指数的值
			// TODO: 这样可能需要额外判断双层根号问题，以及分母有理化问题
			std::map<::Rational, ::Rational> base_ref;
			std::map<::Rational, std::shared_ptr<SymbolicExpr>> exponent_ref;
			std::vector<std::shared_ptr<SymbolicExpr>> result;
			flatten_multiply = [&](const std::shared_ptr<SymbolicExpr>& expr) -> bool {
				if (expr->type == SymbolicExpr::Type::Multiply) {
					for (auto &i : expr->operands) {
						if (!flatten_multiply(i)) return false;
					}
					return true;
				} else if (is_power_compatible(expr)) {
					result.push_back(expr);
				}
				return false;
			};
			// 这样传递可能有性能问题
			// TODO: Debug output:
			std::cerr << "[Debug output] [2] Begin flat operation" << std::endl;
			bool able = flatten_multiply(std::make_shared<SymbolicExpr>(*this));
			// TODO: Debug output:
			std::cerr << "[Debug output] [2] End flat operation" << std::endl;
			if (able) {
				// 尝试合并指数
				bool exponent_merger = true, base_merger = true;
				for (auto &expr : result) {
					auto cvt = power_compatible(expr);
					if (!cvt->operands[0]->is_number()) {
						exponent_merger = false;
					}
					if (!cvt->operands[1]->is_number()) {
						base_merger = false;
						exponent_merger = false;
					}
				}
				
				if (exponent_merger) {
					for (auto &expr : result) {
						auto cvt = power_compatible(expr);
						// 已经检查过了
						::Rational base = cvt->operands[0]->convert_rational();
						::Rational exponent = cvt->operands[1]->convert_rational();
						auto finder = base_ref.find(base);
						if (finder != base_ref.end()) {
							finder->second = finder->second + exponent;
						} else {
							base_ref[base] = exponent;
						}
					}
					// 由于只允许两项乘法，此处需要递归构造
					// 为了后续处理方便，现在构造成 数值*后续表达式 的形式，如果要提升性能可以改二叉树
					auto res = SymbolicExpr::number(1);
					for (auto &i : base_ref) {
						// 不要化简
						res = SymbolicExpr::multiply(SymbolicExpr::power(SymbolicExpr::number(i.first), SymbolicExpr::number(i.second)), res);
					}
					return res;
				} else if (base_merger) {
					for (auto &expr : result) {
						auto cvt = power_compatible(expr);
						auto cvt_rational = cvt->operands[1]->convert_rational();
						auto finder = exponent_ref.find(cvt_rational);
						if (finder != exponent_ref.end()) {
							finder->second = SymbolicExpr::multiply(finder->second, cvt->operands[0])->simplify();
						} else {
							exponent_ref[cvt_rational] = cvt->operands[0];
						}
					}
					// 先这么复制下来，万一以后逻辑不同
					auto res = SymbolicExpr::number(1);
					for (auto &i : exponent_ref) {
						// 不要化简
						res = SymbolicExpr::multiply(SymbolicExpr::power(SymbolicExpr::number(i.first), i.second), res);
					}
					return res;
				}
				
				
			}
			// 否则无法化简，保留原表达式
		}
		
		// 到此处：未能化简，（以后这里可能引入欧拉公式等等）
		// TODO: 分母有理化
		
	}

	return sqrt_and_auxiliary(std::make_shared<SymbolicExpr>(*this), true);
}

std::shared_ptr<SymbolicExpr> SymbolicExpr::simplify_add() const {
    if (operands.size() != 2) return std::make_shared<SymbolicExpr>(*this);

    auto left = operands[0]->simplify();
    auto right = operands[1]->simplify();

    // 解析根号
	// 如果为根号，其中 coeff 为根式的系数，radicand 为根号下的值
    auto extract_sqrt = [](const std::shared_ptr<SymbolicExpr>& expr, ::Rational& coeff, ::Rational& radicand) -> bool {
        if (expr->type == SymbolicExpr::Type::Sqrt && expr->operands.size() == 1 && expr->operands[0]->is_number()) {
            coeff = ::Rational(1);
            radicand = expr->operands[0]->convert_rational();
            return true;
        }
        if (expr->type == SymbolicExpr::Type::Multiply && expr->operands.size() == 2) {
            if (expr->operands[0]->is_number() && expr->operands[1]->type == SymbolicExpr::Type::Sqrt && expr->operands[1]->operands.size() == 1 && expr->operands[1]->operands[0]->is_number()) {
                coeff = expr->operands[0]->convert_rational();
				//std::holds_alternative<::Rational>(expr->operands[0]->get_number()) ? std::get<::Rational>(expr->operands[0]->get_number()) : ::Rational(std::get<int>(expr->operands[0]->get_number()));
				
                radicand = expr->operands[1]->operands[0]->convert_rational();
                return true;
            }
        }
        return false;
    };

    // TODO: 解析变量
    /*auto extract_variable = [](const std::shared_ptr<SymbolicExpr>& expr, ::Rational& coeff) -> bool {
        // type应该是变量或者乘法
        if (expr->type == SymbolicExpr::Type::Variable && expr->operands.size() == 1) {
            return true;
        }
    };*/

    // 解析数字
    auto extract_number = [](const std::shared_ptr<SymbolicExpr> expr, Rational& n) -> bool {
        if (expr->type != SymbolicExpr::Type::Number) return false;
		n = n + expr->convert_rational();
        return true;
    };

    std::vector<std::shared_ptr<SymbolicExpr>> terms;
    std::function<void(const std::shared_ptr<SymbolicExpr>&)> flatten_add;
	// 展开所有加法项
    flatten_add = [&](const std::shared_ptr<SymbolicExpr>& expr) {
        if (expr->type == SymbolicExpr::Type::Add && expr->operands.size() == 2) {
            flatten_add(expr->operands[0]);
            flatten_add(expr->operands[1]);
        } else {
            terms.push_back(expr);
        }
    };
    flatten_add(left);
    flatten_add(right);
    std::map<::Rational, ::Rational> sqrt_terms;// radicand -> sum of coeffs
    // std::map<std::string, ::Rational> variable_terms;// TODO
    Rational number_term(0);// 数字部分
    std::vector<std::shared_ptr<SymbolicExpr>> others;// other things
    for (const auto& term : terms) {
        ::Rational coeff;
        ::Rational radicand;
        if (extract_sqrt(term, coeff, radicand)) {
            sqrt_terms[radicand] = sqrt_terms[radicand] + coeff;
        } else if (extract_number(term, number_term)) {
            // Do nothing yet
        } else {
            others.push_back(term);
        }
    }
    std::vector<std::shared_ptr<SymbolicExpr>> result_terms;
    for (const auto& [radicand, coeff] : sqrt_terms) {
        if (coeff.is_integer() && coeff.get_numerator().to_int() == 0) continue;
        if (coeff == ::Rational(1)) {
            result_terms.push_back(SymbolicExpr::sqrt(SymbolicExpr::number(radicand)));
        } else {
            result_terms.push_back(SymbolicExpr::multiply(SymbolicExpr::number(coeff), SymbolicExpr::sqrt(SymbolicExpr::number(radicand))));
        }
    }
    result_terms.insert(result_terms.end(), others.begin(), others.end());
    if (number_term != 0) {// 非0时才添加数字
        result_terms.push_back(SymbolicExpr::number(number_term));
    }
    if (result_terms.empty()) return SymbolicExpr::number(0);
    if (result_terms.size() == 1) return result_terms[0];

    std::shared_ptr<SymbolicExpr> sum = result_terms[0];
    for (size_t i = 1; i < result_terms.size(); ++i) {
        sum = SymbolicExpr::add(sum, result_terms[i]);
    }
    return sum;
}

std::shared_ptr<SymbolicExpr> SymbolicExpr::simplify_power() const {
    // 如果底数是数字，且指数是整数，用数字储存
    auto base = operands[0]->simplify();
    auto exponent = operands[1]->simplify();
    if (base->is_number() && (exponent->is_int() || exponent->is_big_int())) {
        auto expr = std::make_shared<SymbolicExpr>(Type::Number);
        // 底数是分数，结果为分数
        if (base->is_rational() || exponent->is_rational()) {
            if (exponent->is_int()) {
				expr->number_value = (base->get_rational()).power(BigInt(exponent->get_int()));
            } else if (exponent->is_big_int()) {
				expr->number_value = (base->get_rational()).power(exponent->get_big_int());
            }
        } else {// 否则结果为大整数
            BigInt b;
            if (base->is_int()) {
                b = BigInt(std::get<int>(base->get_number()));
            } else {
                b = std::get<BigInt>(base->get_number());
            }
            BigInt e;
            if (exponent->is_int()) {
                e = BigInt(std::get<int>(exponent->get_number()));
            } else {
                e = std::get<BigInt>(exponent->get_number());
            }
            if (e.to_int() >= 0) {
                expr->number_value = b.power(e);
            } else {
                
                expr->number_value = Rational(BigInt(1), b.power(e.negate()));
            }
        }

        return expr;
    } else if (base->is_number() && exponent->is_rational()) {
        // 底数是数字，指数是分数
        // 用符号储存
		// TODO:某些必要的化简如 8^(1/3)
		auto bsr = base->convert_rational();
		auto expr = exponent->convert_rational();
		
		auto in_range = [](const ::Rational& val) -> bool {
			auto vn = val.get_numerator(), vd = val.get_denominator();
			const ::BigInt lower = ::BigInt(INT_MIN), upper = ::BigInt(INT_MAX);
			return (vn >= lower && vn <= upper) && (vd >= lower && vd <= upper);
		};
		
		if (in_range(bsr) && in_range(expr)) {
			// TODO: Debug output:
			std::cerr << "[Debug output] Power simplifying (rational ^ rational) expressions" << std::endl;
			int bs_n = bsr.get_numerator().to_int(), bs_d = bsr.get_denominator().to_int();
			int es_n = expr.get_numerator().to_int(), es_d = expr.get_denominator().to_int();
			// TODO: Debug output:
			std::cerr << "[Debug output] bs = " << bs_n << "/" << bs_d << "; es = " << es_n << "/" << es_d << std::endl;
			
			// 如果成功，返回 true，origin 为修改后的值，保证 origin 不增大
			// 如果失败，返回 false，origin 不做修改
			// 注意，要保证既约分数（gcd(num, denom) = 1）
			auto simplify_inner = [](int& origin, const int& denom) -> bool {
				// 可以优化
				int answer = 1, target = origin;
				for (int i = 2; 1ll * i * i <= target; i++) {
					int exphere = 0;
					while (target % i == 0) {
						exphere++;
						target /= i;
					}
					if (exphere && (exphere % denom == 0)) {
						int contb = exphere / denom;
						for (int j = 0; j < contb; j++) answer *= i;
					} else return false;
				}
				if (target != 1) {
					if (denom != 1) return false;
					answer *= target;
				}
				// TODO: Debug output:
				std::cerr << "[Debug output] Denom = " << denom << ", Simplifying " << target << " to " << answer << std::endl;
				origin = answer;
				return true;
			};
			
			if (simplify_inner(bs_n, es_d) && simplify_inner(bs_d, es_d)) {
				// 化简成功
				// TODO: Debug output:
				std::cerr << "[Debug output] Post-operation bs = " << bs_n << "/" << bs_d << "; es = " << es_n << "/" << es_d << std::endl;
				std::cerr << "[Debug output] Power simplifying (rational ^ rational) - success" << std::endl;
				auto current_new_base = SymbolicExpr::number((::Rational(bs_n, bs_d)).power(::BigInt(es_n)));
				if (es_n == 1) return current_new_base;
				return SymbolicExpr::power(current_new_base, SymbolicExpr::number(es_n));
			}
			// 否则化简失败，注意 bs_n 和 bs_d 可能需要重新获取
		}
    } else if (base->type == SymbolicExpr::Type::Power || base->type == SymbolicExpr::Type::Sqrt) {
		if (base->type == SymbolicExpr::Type::Sqrt) {
			base = SymbolicExpr::power(base->operands[0], SymbolicExpr::number(::Rational(1, 2)));
		}
		// TODO: Debug output:
		std::cerr << "[Debug output] Power simplifying embedded power / sqrt" << std::endl;
		return SymbolicExpr::power(base->operands[0], SymbolicExpr::multiply(base->operands[1], exponent))->simplify();
	}

    return SymbolicExpr::power(base, exponent);
}


std::string SymbolicExpr::to_string() const {
	
	auto get_output = [](std::shared_ptr<const SymbolicExpr> expr) -> std::string {
		const std::string lbrace = std::string("("), rbrace = std::string(")");
		if (expr->type == SymbolicExpr::Type::Number || expr->type == SymbolicExpr::Type::Variable
			|| expr->type == SymbolicExpr::Type::Sqrt) return expr->to_string();
		else return lbrace + expr->to_string() + rbrace;
	};
	
    switch (type) {
        case Type::Number:
            if (std::holds_alternative<int>(number_value)) {
                return std::to_string(std::get<int>(number_value));
            } else if (std::holds_alternative<::BigInt>(number_value)) {
                return std::get<::BigInt>(number_value).to_string();
            } else if (std::holds_alternative<::Rational>(number_value)) {
                return std::get<::Rational>(number_value).to_string();
            }
            return "0";
            
        case Type::Variable:
            return identifier;
            
        case Type::Sqrt:
            if (operands.empty()) return "√()";
            return "√" + get_output(operands[0]);
            
        case Type::Multiply:
            if (operands.size() < 2) return "*(?)";

            if (operands[0]->is_number() && operands[1]->type == Type::Sqrt) {
                return operands[0]->to_string() + operands[1]->to_string();
            }
            return get_output(operands[0]) + "*" + get_output(operands[1]);
            
        case Type::Add: {
            if (operands.size() < 2) return "+(?)";

            std::vector<const SymbolicExpr*> terms;
            std::function<void(const SymbolicExpr*)> flatten_add;
            flatten_add = [&](const SymbolicExpr* expr) {
                if (expr->type == Type::Add && expr->operands.size() == 2) {
                    flatten_add(expr->operands[0].get());
                    flatten_add(expr->operands[1].get());
                } else {
                    terms.push_back(expr);
                }
            };
            flatten_add(this);

            std::vector<std::string> result_terms;
            for (const auto term : terms) {
                result_terms.push_back(get_output(std::shared_ptr<const SymbolicExpr>(term)));
            }

            if (result_terms.empty()) return "0";
            std::string res = result_terms[0];
            for (size_t i = 1; i < result_terms.size(); ++i) {
                res += "+" + result_terms[i];
            }
            return res;
        }
            
        case Type::Power:
            if (operands.size() < 2) return "^(?)";
            return get_output(operands[0]) + "^" + get_output(operands[1]);
            
        default:
            return "Unknown";
    }
}

double SymbolicExpr::to_double() const {
    switch (type) {
        case Type::Number:
            if (std::holds_alternative<int>(number_value)) {
                return static_cast<double>(std::get<int>(number_value));
            } else if (std::holds_alternative<::BigInt>(number_value)) {
                return std::get<::BigInt>(number_value).to_double();
            } else if (std::holds_alternative<::Rational>(number_value)) {
                return std::get<::Rational>(number_value).to_double();
            }
            return 0.0;

        case Type::Variable:
            if (identifier == "π" || identifier == "pi") {
                #ifdef M_PI
                return M_PI;
                #else
                return 3.14159265358979323846;
                #endif
            }
            // 其他变量仍抛异常
            throw std::runtime_error("Symbolic variable cannot be converted to double");

        case Type::Sqrt:
            if (!operands.empty()) {
                return std::sqrt(operands[0]->to_double());
            }
            return 0.0;

        case Type::Multiply:
            if (operands.size() >= 2) {
                return operands[0]->to_double() * operands[1]->to_double();
            }
            return 0.0;
			
		case Type::Add:
			if (operands.size() >= 2) {
                return operands[0]->to_double() + operands[1]->to_double();
            }
            return 0.0;
			
		case Type::Power:
			return std::pow(operands[0]->to_double(), operands[1]->to_double());

        default:
            return 0.0;
    }
}
