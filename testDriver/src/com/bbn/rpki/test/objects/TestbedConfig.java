/*
 * Created on Dec 9, 2011
 */
package com.bbn.rpki.test.objects;

import java.io.File;
import java.math.BigInteger;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import org.ini4j.Profile.Section;
import org.ini4j.Wini;

import com.bbn.rpki.test.objects.TestbedCreate.FactoryType;
import com.bbn.rpki.test.objects.TestbedCreate.Option;

/**
 * <Enter the description of this type here>
 *
 * @author tomlinso
 */
public class TestbedConfig implements Constants {

  /**
   * Parses the val as a string like AFRINIC,1%APNIC,2%RIPE,2...
   * Stores result as a tuple in the list toMode
   */
  static void parse(List<Pair> toMod, String val) {
    // parse the string like AFRINIC,1%APNIC,2%RIPE,2...
    String[] list = val.split(",");
    for (String item : list) {
      // split the individual groups
      String[] p = item.split("%");
      try {
        Pair pair;
        if (p.length == 1) {
          pair = new Pair(p[0].trim(), null);
        } else {
          pair = new Pair(p[0].trim(), new BigInteger(p[1].trim()));
        }
        toMod.add(pair);
      } catch (Exception e) {
        throw new RuntimeException("Error parsing " + item);
      }
    }
  }

  private final Map<String, FactoryBase> factories = new TreeMap<String, FactoryBase>();
  // Globals for repository specified by configuration file
  // These are set once while parsing the .ini
  private int maxDepth;
  private int maxNodes;

  /**
   * @param factories
   * @param fileName
   */
  TestbedConfig(String fileName) {
    this(new File(fileName));
  }
  
  /**
   * Construct config for the specified file
   * @param file
   */
  public TestbedConfig(File file) {
    try {
      // construct the configparser and read in the config file
      Wini config = new Wini(file);
      Collection<Map.Entry<String, Section>> sectionEntries = config.entrySet();

      // loop over all sections and options and build factories
      for (Map.Entry<String, Section> sectionEntry : sectionEntries) {
        List<Pair> child = new ArrayList<Pair>();
        List<Pair> ipv4 = new ArrayList<Pair>();
        List<Pair> ipv6 = new ArrayList<Pair>();
        List<Pair> as_list = new ArrayList<Pair>();
        List<Pair> roav4l = new ArrayList<Pair>();
        List<Pair> roav6l = new ArrayList<Pair>();
        int a = 0;
        String server = null;
        boolean breakA = false;
        Integer t = 0;
        String subjkeyfile = null;

        String section = sectionEntry.getKey();
        Section sectionMap = sectionEntry.getValue();
        for (Map.Entry<String, String> entry : sectionMap.entrySet()) {

          Option option = Option.valueOf(entry.getKey().toLowerCase());
          if (option == null) {
            System.err.println("Opt in config file not recognized: " + entry.getKey());
            continue;
          }
          String prop = entry.getValue().trim();
          switch (option) {

          case childspec:
            parse(child, prop);
            break;
          case ipv4list:
            parse(ipv4, prop);
            break;
          case ipv6list:
            parse(ipv6, prop);
            break;
          case aslist:
            parse(as_list, prop);
            break;
          case servername:
            server = prop;
            break;
          case breakaway:
            breakA = prop.equalsIgnoreCase("true");
            break;
          case ttl:
            t = new Integer(prop);
            break;
          case max_depth:
            maxDepth = new Integer(prop);
            break;
          case max_nodes:
            maxNodes = new Integer(prop);
            break;
          case roaipv4list:
            parse(roav4l, prop);
            break;
          case roaipv6list:
            // FIXME: maxlength not yet supported
            parse(roav6l, prop);
            break;
          case asid:
            a = new Integer(prop);
            break;
          case subjkeyfile:
            subjkeyfile = prop;
            break;
          }
        }
        String[] typeAndName = section.split("-");
        String type = typeAndName[0];
        String name = typeAndName[1];
        FactoryType factoryType = FactoryType.valueOf(type);
        if (factoryType == null) {
          System.out.println("Unrecognized type included in name of section in the .ini: " + type);
          return;
        }
        FactoryBase f = null;
        switch (factoryType) {
        case C: {
          if ("IANA".equals(name)) {
            f = new IANAFactory(name, child, server, breakA, t, subjkeyfile);
          } else {
            f = new Factory(name, 
                            ipv4,
                            ipv6, 
                            as_list,
                            child,
                            server,
                            breakA,
                            t,
                            subjkeyfile);
          }
          break;
        }
        case M:
          continue;
        case CR:
          continue;
        case R: {
          f = new RoaFactory(name,
                             ipv4,
                             ipv6,
                             as_list, child,
                             server, 
                             breakA,
                             t,
                             roav4l,
                             roav6l, 
                             a);
          break;
        }
        }
        // Add our bluePrintName to the factory dictionary
        factories.put(name, f);
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  /**
   * @return the parsed factories map
   */
  public Map<String, FactoryBase> getFactories() {
    return factories;
  }

  /**
   * @return the maxNodes
   */
  public int getMaxNodes() {
    return maxNodes;
  }

  /**
   * @return
   */
  public int getMaxDepth() {
    return maxDepth;
  }

  /**
   * @param nodeName
   * @return the specified factory
   */
  public FactoryBase getFactory(String nodeName) {
    return factories.get(nodeName);
    
  }
}
