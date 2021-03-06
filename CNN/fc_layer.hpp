#pragma once
#include <math.h>
#include <float.h>
#include <string.h>
#include "layer_t.hpp"

class fc_layer_t: public layer_t
{
public:
	tensor_t<double> activator_input; // Output the sum-the-weights stage.. 
	tensor_t<double> weights; // 2d array of weight (tensor with depth == 1)
	tensor_t<double> act_grad; // gradients for back prop.
        tensor_t<double> old_act_grad;

	fc_layer_t( tdsize in_size, int out_size)
		:
		layer_t(in_size, tdsize(out_size, 1, 1, in_size.b)),
		activator_input(tdsize(out_size, 1, 1, in_size.b)),
		weights( in_size.x*in_size.y*in_size.z, out_size, 1 ),
        	act_grad(tdsize(out_size, 1, 1, in_size.b)),
        	old_act_grad(act_grad.size)
		{
			int maxval = in_size.x * in_size.y * in_size.z;

			for ( int i = 0; i < out_size; i++ )
				for ( int h = 0; h < in_size.x*in_size.y*in_size.z; h++ )
					weights( h, i, 0 ) = 2.19722f / maxval * rand() / double( RAND_MAX );
			// 2.19722f = f^-1(0.9) => x where [1 / (1 + exp(-x) ) = 0.9]
		}

	void change_batch_size(int new_batch_size) {
		std::cout << "Changing fc_layer batch_size" << std::endl;
		layer_t::change_batch_size(new_batch_size);
                tensor_t<double> new_act(tdsize(activator_input.size.x, 1, 1, new_batch_size));
                activator_input = new_act;
		tensor_t<double> new_act_grad(tdsize(out.size.x, 1, 1, in.size.b));
		act_grad = new_act_grad;
		tensor_t<double> new_old_act_grad(act_grad.size);
		old_act_grad = new_old_act_grad;
	}

	double activator_function( double x ) {
		// THis is the logistic function.  Detail here: https://en.wikipedia.org/wiki/Logistic_function#Derivative
		double sig = 1.0f / (1.0f + exp( -x ));
		return sig;
	}

	double activator_derivative( double x ) {
		double sig = 1.0f / (1.0f + exp( -x ));
		return sig * (1 - sig);
	}
#if(0)
	void activate( tensor_t<double>& in ) {
		copy_input(in);

		for ( uint n = 0; n < activator_input.element_count(); n++ ) {
			activator_input.data[n] = 0;
		}
		
		// Here's the math we are doing here:
		//
		// We'll use some short hand:
		//
		// x = in
		// w = weights
		// L = activator_function  
		// f(x,w) = x*w
		//
		// This layer is a function, F, that maps `x` to `out`
		//
		// out = F(x,w) = L(f(x,w))
		// 
		// f(x,w) = x*w is vector-matrix product which yields a vector.
		// We apply L to each element to get the output vector.
		for ( int n = 0; n < out.size.x; n++ ) {


			// Both `in` and `weights` are tensors instead
			// of vectors in this code.

			// compute the dot product.
			for ( uint i = 0; i < in.element_count(); i++ ) {
				activator_input.as_vector(n) += in.as_vector(i) * weights( i, n, 0 );
			}
		}

		// finally, apply the activator function.
		for ( int n = 0; n < out.size.x; n++ ) {
			out( n, 0, 0 ) = activator_function( activator_input.as_vector(n) );
		}

	}
#endif

	void activate( tensor_t<double>& in ) {
		copy_input(in);

		tdsize old_size = in.size;
		tdsize old_out_size = out.size;

		// cast to correct shape
		in.size.x = old_size.x * old_size.y * old_size.z;
		in.size.y = old_size.b;
		in.size.z = 1;
		in.size.b = 1;

		out.size.x = old_out_size.x * old_out_size.y * old_out_size.z;
		out.size.y = old_out_size.b;
		out.size.z = 1;
		out.size.b = 1;

		for ( int b = 0; b < activator_input.size.b; b++) {
			for ( int n = 0; n < activator_input.size.x; n++ ) {
				activator_input(n, 0, 0, b) = 0;
			}
		}

		for ( int b = 0; b < in.size.y; b++ ) {
			for ( int i = 0; i < in.size.x; i++ ) {
				for ( int n = 0; n < out.size.x; n++ ) {
					double in_val = in(i, b, 0);
					double weight_val = weights( i, n, 0 );
					double mul_val = in_val * weight_val;
					double acc_val = activator_input(n, 0, 0, b) + mul_val;
					activator_input(n, 0, 0, b) = acc_val;
				}
			}
		}

		// finally, apply the activator function.
		for ( unsigned int n = 0; n < activator_input.element_count(); n++ ) {
			out.data[n] = activator_function( activator_input.data[n] );
		}

		// don't forget to reset the shapes
		in.size = old_size;
		out.size = old_out_size;
	}

	void calc_grads( const tensor_t<double>& grad_next_layer ) {
		
		memset( grads_out.data, 0, grads_out.size.x * grads_out.size.y * grads_out.size.z * sizeof( double ) );

		// Using the notation from activate():
		//
		// We do two things in the loop below: 1) compute the
		// gradient (grad.grad). 2) propagate the error to the
		// previous error.

		// The gradient:
		
		// We are calculating the derivative of F with respect
		// to w.  The derivative, F', is a vector, so it's a
		// gradient (stored in `gradients`)
		//
		// F(x, w)  = L(f(x,w)) // from above
		// F'(x, w) = L'(f(x,w)) * f'(x,w)
		//
		// To compute index, i, of the F', we calculate the
		// derivative with respect to w[i].
		//
		// Note that
		//
		// f(x,w) = x[0]*w[0] + x[1]*w[1] + ... + x[n]*w[n]
		//
		// So the derivative wrt w[i] is 
		//
		// df(x,w)/dw[i] = x[i]
		//
		// L'(x) = activator_derivative(x) // look at the code, if you're curious.

		// The inner loop is responsible for back-propagating
		// the error.  Intuitively, we are assigning 'blame'
		// for the error in this layer's output to the
		// elements of the input tensor.
		//
		// The amount of blame we assign to each input for the
		// error in a particular output is proportional to
		// that input's weight for that output.  If the weight
		// for an input is large, it had a large impact on the
		// output, so it more responsible for the resulting
		// error.

		// The errors attributed to each input is the sum of
		// the error it contributed across all the outputs.

                grads_out.size.x = grads_out.size.x * grads_out.size.y * grads_out.size.z;
                grads_out.size.y = 1;
                grads_out.size.z = 1;

                for ( int b = 0; b < out.size.b; b++ ) {
                        for ( int n = 0; n < activator_input.size.x; n++ ){
				// In `activate()` we saved the value of
				// f(x,w) as `activator_input`, so we are
				// reusing it here to compute L'(f(x,w))
				double ad = activator_derivative( activator_input(n, 0, 0, b) );
				//std::cout << ad;
				double ng = grad_next_layer(n, 0, 0, b);
				//std::cout << ng;
				act_grad(n, 0, 0, b) = ad * ng;
                        }
                }
		
		// We are calculating how much each input
		// contributed to the error.  That
		// contribution is proportional to the
		// weights.
		
                for ( int b = 0; b < out.size.b; b++ ) {
			for ( int i = 0; i < grads_out.size.x; i++ ) {
				for ( int n = 0; n < out.size.x; n++ ) {
                                        grads_out(i, 0, 0, b) += act_grad(n, 0, 0, b) * weights( i, n, 0);
                                }
                        }
                }

		grads_out.size = in.size;
	}
	
	void fix_weights() {
		// Here, we are updating the weights.  The amount we
		// change the input primarily depends on the gradient
		// and the input value.  We use gradient decent, which
		// means we follow the gradient downward to minimize
		// error.
		//
		// Recall that during back propagation, the input the
		// layer is the error and the derivatives are with
		// respect to the weights.  This means that the
		// gradient points in the direction we should move the
		// weights to reduce the error.
		//
		// We calculated the gradientt in calc_grads(), and
		// proportional to the error (i.e., grad_next_layer)
		// and the derivative of the activator function.  This
		// means that larger errors or steeper slopes causes
		// bigger changes in the weights.
		//
		// The basic update rule is
		//
		// w_new = w - gradient * input
		//
		// This update rule is too aggressive, however, so we
		// add a learning rate, u:
		//
		// w_new = w - gradient * input * u
		//
		// There is a also problem that can arise when the
		// gradient get small: progress toward the minimum can
		// slow.  So we also have a "momentum" term, M:
		//
		// t = gradient + old_gradient*momentum
		// w_new = w - u * input * t
		//
		// Finally, to smooth out the changes in gradient, we
		// add a 'decay' term governed by a decay, D:
		//
		// t = gradient + old_gradient*momentum
		// w_new = w - (u * input * t + D * w)
		//
		// All this complication lives in update_weight()
		// 
		// Since the above needs old_gradient, the gradient
		// tensor has the old and new gradient values in it.
		// update_gradient() updates the old gradient with the
		// new value.

		tdsize old_in_size = in.size;
		in.size.x = in.size.x * in.size.y * in.size.z;
		in.size.y = 1;
		in.size.z = 1;

                for ( int b = 0; b < out.size.b; b++ ) {
                //{ int b = 1;
                        for ( int n = 0; n < weights.size.y; n++ ) {
                                for ( int i = 0; i < weights.size.x; i++ ) {
                                        double& w = weights( i, n, 0 );
                                        double m = (act_grad(n, 0, 0, b) + old_act_grad(n, 0, 0, b) * MOMENTUM);
                                        double g_weight = w - (LEARNING_RATE * m * in(i, 0, 0, b) + LEARNING_RATE * WEIGHT_DECAY * w);
                                        w = g_weight;
                                }
                                old_act_grad(n, 0, 0, b) = act_grad(n, 0, 0, b) + old_act_grad(n, 0, 0, b) * MOMENTUM;
                        }
                }
		in.size = old_in_size;
	}

	// The rest is just utility functions
	size_t get_total_memory_size() const {
		return weights.get_total_memory_size() +
			act_grad.element_count() * sizeof(double) +
			old_act_grad.element_count() * sizeof(double) +
			activator_input.element_count() * sizeof(double) +
			layer_t::get_total_memory_size();
	}

	std::string kind_str() const {
		return "fc";
	}
	std::string param_str() const {
		std::stringstream ss;
		return ss.str();
	}

	bool operator==(const fc_layer_t & o) const {
		if (o.weights != weights) return false;
		if (o.in != in) return false;
		if (o.grads_out != grads_out) return false;
		if (o.out != out) return false;
		return true;
	}

	bool operator!=(const fc_layer_t & o) const {
		return !(*this == o);
	}

	virtual ~fc_layer_t(){}
	
	virtual std::string analyze_inequality_with(layer_t* other) {
		auto _other = dynamic_cast<fc_layer_t*>(other);
		throw_assert(_other, "You called 'analyze_inequality_with' without a mismatched layer type")
		std::stringstream out;
		if (this->activator_input.size != _other->activator_input.size) {
			out << "Activator_Input sizes don't match: " << DUMP(this->activator_input.size) << " != " << DUMP(_other->activator_input.size) << "\n";
		}
		if (this->weights.size != _other->weights.size) {
			out << "Weights sizes don't match: " << DUMP(this->weights.size) << " != " << DUMP(_other->weights.size) << "\n";
		}
		if (this->act_grad.size != _other->act_grad.size) {
			out << "Act_grad sizes don't match: " << DUMP(this->act_grad.size) << " != " << DUMP(_other->act_grad.size) << "\n";
		}
		if (this->old_act_grad.size != _other->old_act_grad.size) {
			out << "Old_act_grad sizes don't match: " << DUMP(this->old_act_grad.size) << " != " << DUMP(_other->old_act_grad.size) << "\n";
		}
		//if (this->gradients.size() != _other->gradients.size()) {
		//	out << "Gradients sizes don't match: " << DUMP(this->gradients.size()) << " != " << DUMP(_other->gradients.size()) << "\n";
		//}
		
		out << this->layer_t::analyze_inequality_with(_other);
	
		out << "Diff of ->activator_input: " << diff(this->activator_input, _other->activator_input) << "\n";
		out << "Diff of ->weights: " << diff(this->weights, _other->weights) << "\n";
		out << "Diff of ->act_grad: " << diff(this->act_grad, _other->act_grad) << "\n";
		out << "Diff of ->old_act_grad: " << diff(this->old_act_grad, _other->old_act_grad) << "\n";
		//out << "Diff of ->gradients: " << diff(this->gradients, _other->gradients) << "\n";
		return out.str();
	}
	
};

template<class T> T* run_fc(int x, int y, int z, int b,
			    int out_size,
			    int seed) {
	srand(seed);
	tdsize size(x,y,z,b);
	T * l = new T( size, out_size);
	l->test_me();
	return l;
}

template<class T> T* run_fc_activate(int x, int y, int z, int b,
				     int out_size,
				     int seed) {
	srand(seed);
	tdsize size(x,y,z,b);
	T * l = new T( size, out_size);
	l->test_activate();
	return l;
}
template<class T> T* run_fc_calc_grads(int x, int y, int z, int b,
				       int out_size,
				       int seed) {
	srand(seed);
	tdsize size(x,y,z,b);
	T * l = new T( size, out_size);
	l->test_calc_grads();
	return l;
}

template<class T> T* run_fc_fix_weights(int x, int y, int z, int b,
					int out_size,
					int seed) {
	srand(seed);
	tdsize size(x,y,z,b);
	T * l = new T( size, out_size);
	l->test_fix_weights();
	return l;
}

template<class T>
void fc_test(int x, int y, int z, int b, int out, int seed) {					
	fc_layer_t * reference = run_fc<fc_layer_t>(x,y,z,b,out,seed); 
	fc_layer_t * optimized = run_fc<T>(x,y,z,b,out,seed); 
	EXPECT_LAYERS_EQ(fc_layer_t, reference, optimized) << "Failure: fc_test("<< x << ", " << y<< ", " << z<< ", "  << b << ", " << out << ", " << seed << ");\n";
	delete reference;					
	delete optimized;
}


template<class T>
void fc_test_activate(int x, int y, int z, int b, int out, int seed) {
	fc_layer_t * reference = run_fc_activate<fc_layer_t>(x,y,z,b,out,seed); 
	fc_layer_t * optimized = run_fc_activate<T>(x,y,z,b,out,seed); 
	EXPECT_TENSORS_EQ(double, reference->out, optimized->out) << "Failure: fc_test_activate("<< x << ", " << y<< ", " << z<< ", " << b << ", "<< out << ", " << seed << ");\n";
	delete reference;					
	delete optimized;
}

template<class T>
void fc_test_calc_grads(int x, int y, int z, int b,  int out, int seed) {					
	fc_layer_t * reference = run_fc_calc_grads<fc_layer_t>(x,y,z,b,out,seed); 
	fc_layer_t * optimized = run_fc_calc_grads<T>(x,y,z,b,out,seed); 
	EXPECT_TENSORS_EQ(double, reference->grads_out, optimized->grads_out) << "Failure: fc_test_calc_grads("<< x << ", " << y<< ", " << z<< ", " << b << ", " << out << ", " << seed << ");\n";
	delete reference;					
	delete optimized;
}

template<class T>
void fc_test_fix_weights(int x, int y, int z, int b, int out, int seed) {
	fc_layer_t * reference = run_fc_fix_weights<fc_layer_t>(x,y,z,b,out,seed); 
	fc_layer_t * optimized = run_fc_fix_weights<T>(x,y,z,b,out,seed); 
	EXPECT_TENSORS_EQ(double, reference->weights, optimized->weights) << "Failure: fc_test_fix_weights("<< x << ", " << y<< ", " << z<< ", "  << b << ", " <<  out << ", " << seed << ");\n";
	delete reference;					
	delete optimized;
}


#ifdef INCLUDE_TESTS
namespace CNNTest{

	TEST_F(CNNTest, fc_simple) {
		
		tdsize in_size(10,10,10);
		int  out_size = 5;
		fc_layer_t t1(in_size, out_size);
		fc_layer_t t2(in_size, out_size);
		tensor_t<double> in(in_size);
		randomize(in);
		t1.activate(in);
		EXPECT_EQ(t1,t1);
		EXPECT_NE(t1,t2);

	}

}  // namespace
#endif


	
