package uk.ac.imperial.lsds.crossbow.kernel;

import java.nio.BufferOverflowException;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import uk.ac.imperial.lsds.crossbow.Batch;
import uk.ac.imperial.lsds.crossbow.Operator;
import uk.ac.imperial.lsds.crossbow.data.IDataBuffer;
import uk.ac.imperial.lsds.crossbow.device.TheGPU;
import uk.ac.imperial.lsds.crossbow.kernel.conf.ReLUConf;
import uk.ac.imperial.lsds.crossbow.model.LocalVariable;
import uk.ac.imperial.lsds.crossbow.model.Model;
import uk.ac.imperial.lsds.crossbow.model.Shape;
import uk.ac.imperial.lsds.crossbow.model.Variable;
import uk.ac.imperial.lsds.crossbow.task.ITask;
import uk.ac.imperial.lsds.crossbow.types.ModelAccess;

public class ReLUGradient extends Kernel {
	
	private final static Logger log = LogManager.getLogger (ReLUGradient.class);
	
	ReLUConf conf;
	
	public ReLUGradient (ReLUConf conf) {
		this.conf = conf;
	}
	
	public ReLUGradient setup (Shape [] inputShape, Model model) {
		
		log.debug(String.format("Setup kernel for operator %s", operator.getName()));
		
		if (inputShape.length > 1)
			throw new IllegalArgumentException (String.format("error: invalid number of inputs for operator %s", operator.getName()));
		
		Variable input = new Variable ("input", inputShape[0], true);
		theInput = new LocalVariable (input);
		
		log.debug(String.format("Input variable %s", input.getName()));
		
		/* Configure the output shape
		 * 
		 * The output of a gradient operator has the same 
		 * shape as the input of its forward peer.
		 * 
		 * But note that the peer could have more than one inputs.
		 */
		Operator peer = operator.getPeer();
		Shape [] p = peer.getInputShape();
		if (p.length > 1)
			throw new IllegalStateException(String.format("error: peer operator %s has more than one inputs", peer.getName()));
		
		outputShape = p[0].copy();
		
		Variable output = new Variable ("output", outputShape, true);
		theOutput = new LocalVariable (output);
		
		log.debug(String.format("Output variable %s", output.getName()));
		
		/* 
		 * Set memory requirements 
		 */
		
		/* Set output, by default */
		memoryRequirements.setOutputMemoryRequirements(output.capacity());
		
		/* Are there any model variables? No */
		memoryRequirements.setModelMemoryRequirements (0);
		
		/* Are there any CPU-specific local variables? No */
		memoryRequirements.setLocalCPUMemoryRequirements (0);
		
		/* Are there any GPU-specific local variables? No */
		memoryRequirements.setLocalGPUMemoryRequirements (0);
		
		return this;
	}
	
	public void GPURegister () {
		
		log.debug(String.format("Register kernel with GPU for operator %s", operator.getName()));
		
		int id = operator.getId();
		String name = this.getClass().getSimpleName();
		
		/* 1 input, 0 local variables, 1 output */
		TheGPU.getInstance().setKernel (id, name, 1, 0, 1, (isLossKernel() || isAccuracyKernel()));
		
		Variable []  input =  theInput.getInitialValue();
		Variable [] output = theOutput.getInitialValue();
		
		/* Set input */
		TheGPU.getInstance().setKernelInput  (id, 0,  input[0].getShape().array(),  input[0].capacity());
		
		/* Set output */
		TheGPU.getInstance().setKernelOutput (id,    output[0].getShape().array(), output[0].capacity());
		
		/* Set kernel configuration parameters */
		TheGPU.getInstance().setKernelConfigurationParameters (id, 1);
		TheGPU.getInstance().setKernelConfigurationParameterAsFloat (id, 0, "slope", conf.getNegativeSlope());
	}
	
	public void compute (Operator [] previous, Batch batch, Model model, ITask api) {

		log.debug(String.format("Compute kernel for operator %s", operator.getName()));

		if (previous != null && previous.length > 1)
			throw new IllegalArgumentException (String.format("error: invalid number of inputs for operator %s", operator.getName()));
		
		/* Get thread-local variables */
		Variable []  input =  theInput.get();
		Variable [] output = theOutput.get();
		
		/* Get input buffer */
		IDataBuffer inputDataBuffer = getCurrentInput (batch, api);
		int inputStartP = getStartPointer ();
		int inputEndP = getEndPointer ();
		
		/* Get an output buffer */
		IDataBuffer outputDataBuffer = getCurrentOutput (batch, api);
		output[0].wrap(outputDataBuffer);
		
		/* Get peer input */
		IDataBuffer peerInputDataBuffer = getPeerInput (batch, api);
		
		/* Get configuration variable(s) */
		float slope = conf.getNegativeSlope();
		
		int elements = input[0].getShape().countAllElements();
		
		int offset, outputOffset, inputOffset;
		float inputValue, peerInputValue;
		
		for (int ndx = 0; ndx < elements; ++ndx) {
			
			offset = ndx * input[0].getType().sizeOf();
			
			 inputOffset = offset + inputStartP;
			outputOffset = offset;
			if (inputOffset >= inputEndP)
				throw new BufferOverflowException();
			
			inputValue = inputDataBuffer.getFloat(inputOffset);
			/* 
			 * TODO 
			 * 
			 * Assumes that the peer operator is not the most upstream operator, 
			 * otherwise its input would have been the raw input buffer and we
			 * would have to take into account the offset to the latter.  
			 */
			peerInputValue = peerInputDataBuffer.getFloat(outputOffset);

			if (peerInputValue > 0) {
				outputDataBuffer.putFloat(outputOffset, inputValue);
			} 
			else {
				outputDataBuffer.putFloat(outputOffset, slope * inputValue);
			}
		}
		
		/* Store output in batch for downstream operators */
		batch.setOutput(operator.getId(), outputDataBuffer);
	}
	
	public ModelAccess getModelAccessType () {
		return ModelAccess.NA;
	}
	
	public boolean isLossKernel () {
		return false;
	}
	
	public boolean isAccuracyKernel () {
		return false;
	}
	
	public boolean isDataTransformationKernel () {
		return false;
	}
	
	public boolean allowsOutputOverwrite () {
		return false;
	}
	
	public boolean allowsInputOverwrite () {
		return false;
	}
}
