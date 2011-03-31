/**
 * Project: Assignment_04_SimpleVideo
 * File: Buffer.java
 * Date: 2011-03-31
 * Time: 7:30:47 AM
 */
package org.trollop.SimpleVideo;

import java.util.Vector;

import android.util.Log;

/**
 * This class implements a buffer that can be used by threads to exchange
 * data. The threads that get blocked because the buffer is full or
 * empty are kept in a queue.
 *
 * @author Steffen L. Norgren, A00683006
 */
public class Buffer {
	
	/** The capacity of the buffer. */
	private int capacity;
	
	/** The elements of the buffer. */
	private Vector<byte[]> elements;
	
	/** The queue of threads waiting until the buffer is not full or empty. */
	private Vector<Thread> queue;
	
	/**
	 * Instantiates a new buffer.
	 *
	 * @param capacity the capacity of the buffer
	 */
	public Buffer(int capacity) {
		this.capacity = capacity;
		elements = new Vector<byte[]>(capacity);
		queue = new Vector<Thread>();
	}
	
	/**
	 * Returns the size of the buffer.
	 *
	 * @return the size of the buffer.
	 */
	public int size() {
		return elements.size();
	}
	
	/**
	 * Adds an element to the end of the buffer. If the buffer is full, the
	 * calling thread will be blocked until another thread removes an element
	 * from the buffer.
	 *
	 * @param element the element to be added to the buffer.
	 */
	public synchronized void put(byte[] element) {
		Thread caller = Thread.currentThread();
		
		if (elements.size() == capacity) {
			queue.addElement(caller);
			
			while (size() == capacity || caller != queue.firstElement()) {
				try {
					wait();
				}
				catch (InterruptedException e) {
					Log.e("put: InterruptedException", e.toString());
				}
			}
			queue.removeElement(caller);
		}
		elements.addElement(element);
		notifyAll();
	}
	
	/**
	 * Removes the first element from the buffer. If the buffer is empty, the
	 * calling thread will be blocked until another thread adds an element to
	 * the buffer.
	 *
	 * @return the byte[] that was removed from the buffer.
	 */
	public synchronized byte[] get() {
		Thread caller = Thread.currentThread();
		
		if (elements.isEmpty()) {
			queue.addElement(caller);
			
			while (elements.isEmpty() || caller != queue.firstElement()) {
				try {
					wait();
				}
				catch (InterruptedException e) {
					Log.e("get: InterruptedException", e.toString());
				}
			}
			queue.removeElement(caller);
		}
		byte[] element = elements.firstElement();
		elements.removeElement(element);
		notifyAll();
		return element;
	}
}
